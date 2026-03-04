#pragma once

#include "utils.h"

NAMESPACE_BEGIN(Allocator)

template<size_t Alignment>
constexpr size_t alignSize(size_t size) {
	if (size == 0) return 0;
	return (size + Alignment - 1) & ~(Alignment - 1);
}

// The following composable allocator system is based on the following talk from CppCon 2015: https://www.youtube.com/watch?v=LIb3L4vKZ7U 
/* 
	static constexpr u8 alignment;
	static constexpr size_t goodSize(size_t size);
	Block allocate(size_t size);
	Block allocateAll();
	bool expand(Block &block, size_t delta);
	void reallocate(Block &block, size_t newSize);
	bool owns(Block block);
	void deallocate(Block block);
	void deallocateAll();
*/

// These methods can never be implemented, if the operation is not always possible:
// allocateAll, reallocate, owns, deallocateAll

// These methods can be implemented, even if the operation is not always possible:
// allocate, deallocate, expand

template<typename T>
struct TypedBlock { 
	T *ptr = nullptr; 
	size_t size = 0; 

	template<typename U> TypedBlock<T> &operator=(TypedBlock<U> &other) {
		ptr = reinterpret_cast<T *>(other.ptr);
		size = other.size;
		return *this;
	}

	template<typename U> operator TypedBlock<U>() {
		return {
			.ptr = reinterpret_cast<U *>(ptr),
			.size = size
		};
	}

	bool operator==(const TypedBlock<T> &other) {
		return ptr == other.ptr && size == other.size;
	}
};

using Block = TypedBlock<u8>;

#if ENABLE_DEBUG
#define DEALLOCATE_NULL_CHECK(block) if (block.ptr == nullptr) { LOG_ERROR("Tried to deallocate a null block."); return; }
#else
#define DEALLOCATE_NULL_CHECK(block) if (block.ptr == nullptr) return;
#endif

// Allocator primitives.

class FailureStub {
public:
	static constexpr bool HAS_INFINITE_FALLBACK = true; // NOTE(Erik): Obviously this shouldn't be used in production.

	FailureStub() = default;
	FailureStub(const FailureStub &) = delete;
	static constexpr size_t goodSize(size_t size) {
		return 0;
	}

	Block allocate(size_t size);
	Block allocateAll();
	bool expand(Block &block, size_t delta);
	void reallocate(Block &block, size_t newSize);
	bool owns(Block block);
	void deallocate(Block block);
	void deallocateAll();
};

class Malloc {
public:
	static constexpr bool HAS_INFINITE_FALLBACK = true;
	static constexpr size_t alignment = alignof(std::max_align_t);

	static constexpr size_t goodSize(size_t size) {
		return alignSize<alignment>(size);
	}

	Malloc() {}

	Malloc(const Malloc &) = delete;

	Malloc(Malloc &&) = default;

	Block allocate(size_t size);
	bool expand(Block &block, size_t delta);
	void reallocate(Block &block, size_t newSize);
	bool owns(Block block);
	void deallocate(Block block);
};

template<size_t Size, bool Static = true>
class Contiguous {
	struct StaticMemory {
		alignas(std::max_align_t) u8 array[Size] = {};
	};

	struct DynamicMemory {
		u8 *array = 0;
		DynamicMemory() : array((u8 *)calloc(Size, 1)) { }
		~DynamicMemory() { if (array != nullptr) { free(array); } }
	};

	using Memory = std::conditional_t<Static, StaticMemory, DynamicMemory>;
	Memory memory = {};
	size_t used = 0;

public:
	static constexpr bool HAS_INFINITE_FALLBACK = false;
	static constexpr size_t alignment = alignof(std::max_align_t);

	static constexpr size_t goodSize(size_t size) {
		return alignSize<alignment>(size);
	}

	Contiguous() {}

	Contiguous(const Contiguous &) = delete;

	Contiguous(Contiguous &&other) {
		memory = std::move(other.memory);
		used = other.used;
		if constexpr (!Static) {
			other.memory.array = {};
			other.used = 0;
		}
	}

	Block allocate(size_t size) {
		Block block = {};
		const size_t gSize = goodSize(size);
		if (used + gSize <= Size) {
			block = { memory.array + used, size };
			used += gSize;
		}
		return block;
	}

	bool expand(Block &block, size_t delta) {
		const size_t usedIncrease = goodSize(block.size + delta) - goodSize(block.size);
		if (block.ptr + goodSize(block.size) == memory.array + used && used + usedIncrease <= Size) {
			block.size += delta;
			used += usedIncrease;
			return true;
		}
		return false;
	}

	void reallocate(Block &block, size_t newSize) {
		block = allocate(newSize);
	}

	bool owns(Block block) {
		return memory.array <= block.ptr && block.ptr < memory.array + used;
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		const size_t gSize = goodSize(block.size);
		if (block.ptr + gSize == memory.array + used) {
			used -= gSize;
		}
	}

	void deallocateAll() {
		used = 0;
	}
};

// Enable reference based or value based constructor using the CRTP pattern.
template<class A> struct RefStorage { A &a; RefStorage(A &a) : a(a) {} };
template<class A> struct ValStorage { A a; ValStorage(A a) : a(std::move(a)) {} };
template<class A> struct NoStorage { A a; NoStorage() : a() {} };

template<class P, class S> class Fallback {
	P &p;
	S &s;

public:
	static_assert(P::alignment == S::alignment);
	static constexpr bool HAS_INFINITE_FALLBACK = S::HAS_INFINITE_FALLBACK;
	static constexpr size_t alignment = P::alignment;

	static constexpr size_t goodSize(size_t size) {
		return P::goodSize(size);
	}

	Fallback(P &p, S &s) : p(p), s(s) {}
	Fallback(const Fallback &) = delete;

	Block allocate(size_t size) {
		Block block = p.allocate(size);
		if (block.ptr == nullptr) {
			block = s.allocate(size);
		}
#if ENABLE_DEBUG
		if (block.ptr == nullptr) {
			LOG_ERROR("A block could not be allocated be the primary or secondary allocator.");
		}
#endif
		return block;
	}

	bool expand(Block &block, size_t delta) {
		if (p.owns(block)) {
			return p.expand(block, delta);
		}
#if ENABLE_DEBUG
		if (!s.owns(block)) {
			LOG_ERROR("A block is not owned by the primary- or the secondary allocator.");
		}
#endif
		return s.expand(block, delta);
	}

	void reallocate(Block &block, size_t newSize) {
		if (p.owns(block)) {
			Block reallocBlock = block;
			p.reallocate(reallocBlock, newSize);

			if (reallocBlock.ptr == nullptr) {
				block = s.allocate(newSize);
				assert(s.owns(block));
			} else {
				block = reallocBlock;
				return;
			}
		}
#if ENABLE_DEBUG
		if (!s.owns(block)) {
			LOG_ERROR("A block is not owned by the primary- or the secondary allocator.");
		}
#endif
		s.reallocate(block, newSize);
	}

	bool owns(Block block) {
		return p.owns(block) || s.owns(block);
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		if (p.owns(block)) {
			p.deallocate(block);
			return;
		}
#if ENABLE_DEBUG
		if (!s.owns(block)) {
			LOG_ERROR("A block is not owned by the primary- or the secondary allocator.");
		}
#endif
		s.deallocate(block);
	}

};

template<size_t threshold, class SmallAllocator, class LargeAllocator> 
class Segregator {
	SmallAllocator &s;
	LargeAllocator &l;

public:
	static constexpr bool HAS_INFINITE_FALLBACK = SmallAllocator::HAS_INFINITE_FALLBACK && LargeAllocator::HAS_INFINITE_FALLBACK;
	static_assert(SmallAllocator::alignment == LargeAllocator::alignment);
	static constexpr size_t alignment = SmallAllocator::alignment;

	static constexpr size_t goodSize(size_t size) {
		return SmallAllocator::goodSize(size);
	}

	Segregator(SmallAllocator &s, LargeAllocator &l) : s(s), l(l) {}
	Segregator(const Segregator &) = delete;

	Block allocate(size_t size) {
		if (size <= threshold) {
			return s.allocate(size);
		} else {
			return l.allocate(size);
		}
	}

	bool expand(Block &block, size_t delta) {
		if (block.size + delta <= threshold) {
			return s.expand(block, delta);
		} else if (block.size > threshold && block.size + delta > threshold) {
			return l.expand(block, delta);
		} else { // block.size <= threshold && block.size + delta > threshold
			return false;
		}
	}

	void reallocate(Block &block, size_t newSize) {
		assert(newSize >= block.size);
		if (block.size <= threshold && newSize <= threshold) {
			s.reallocate(block, newSize);
		} else if (block.size > threshold && newSize <= threshold) {
			Block newBlock = s.allocate(newSize);
			memcpy(newBlock.ptr, block.ptr, block.size);
			l.deallocate(block);
			block = newBlock;
		} else { // block.size > threshold && newSize > threshold
			l.reallocate(block, newSize);
		}
	}

	bool owns(Block block) {
		if (block.size <= threshold) {
			return s.owns(block);
		} else {
			return l.owns(block);
		}
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		if (block.size <= threshold) {
#if ENABLE_DEBUG
			if (!s.owns(block)) {
				LOG_ERROR("Deallocation failed since secondary allocator does not own a block.");
				return;
			}
#endif
			s.deallocate(block);
		} else {
			return l.deallocate(block);
		}
	}

};

/**
 * The purpose of a freelist is to track the allocations individually and allow for fine-grained allocation / deallocation.
 * A freelist sits on top of a parent allocator. A freelist owns its' parent allocator.
 * If an allocation has a size between `MinAllocSize` (inclusive) and `MaxAllocSize` (inclusive), it can be tracked in the list. Otherwise, the allocation falls through to the parent allocator.
 */
template<
	class A, size_t MinAllocSize, size_t MaxAllocSize, 
	template<class> class Storage = RefStorage
> 
class Freelist : public Storage<A> {
	using Storage<A>::a;
	struct Node { Node *next; } *root = nullptr;

public:
	static constexpr bool HAS_INFINITE_FALLBACK = A::HAS_INFINITE_FALLBACK;
	static constexpr size_t alignment = A::alignment;

	static constexpr size_t goodSize(size_t size) {
		return A::goodSize(size);
	}

	static constexpr size_t gMaxAllocSize = goodSize(MaxAllocSize);
	static_assert(gMaxAllocSize == MaxAllocSize);

	template<typename... Args>
	Freelist(Args&&... args) : Storage<A>(std::forward<Args>(args)...) { }
	Freelist(const Freelist &) = delete;

	bool isBlockOwnedByFreelist(const size_t size) {
		return MinAllocSize <= size && size <= MaxAllocSize;
	}

	Block allocate(size_t size) {
		const size_t gSize = goodSize(size);
		if (isBlockOwnedByFreelist(gSize)) {
			if (root == nullptr) {
				Block block = a.allocate(gMaxAllocSize);
				return {
					.ptr = block.ptr,
					.size = size
				};
			}
			u8 *ptr = reinterpret_cast<u8 *>(root);
			root = root->next;
			return {
				.ptr = ptr,
				.size = size
			};
		}
		return a.allocate(size);
	}

	bool expand(Block &block, size_t delta) {
		const size_t gSize = goodSize(block.size);
		if (isBlockOwnedByFreelist(gSize)) {
			const size_t newSize = block.size + delta;
			const size_t gNewSize = goodSize(newSize);
			if (gNewSize <= MaxAllocSize) {
				block.size = newSize;
				return true;
			} else {
				return false;
			}
		}
		return false;
	}

	void reallocate(Block &block, size_t newSize) {
		const size_t gSize = goodSize(block.size);
		if (isBlockOwnedByFreelist(gSize)) {
			const size_t gNewSize = goodSize(newSize);
			if (newSize <= MaxAllocSize) {
				block.size = newSize;
			} else {
				Node *newRoot = reinterpret_cast<Node *>(block.ptr);
				newRoot->next = root;
				root = newRoot;
				block = a.allocate(gNewSize);
			}
			return;
		}
		a.reallocate(block, newSize);
	}

	bool owns(Block block) {
		return isBlockOwnedByFreelist(goodSize(block.size)) || a.owns(block);
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		if (isBlockOwnedByFreelist(goodSize(block.size))) {
			Node *newNode = reinterpret_cast<Node *>(block.ptr);
			newNode->next = root;
			root = newNode;
		} else {
			a.deallocate(block);
		}
	}

	void deallocateAll() {
		root = nullptr;
		a.deallocateAll();
	}

};

#define FALLTHROUGH()  \
	static constexpr u8 alignment = A::alignment; \
	static constexpr size_t goodSize(size_t size) { return A::goodSize(size); } \
	Block allocate(size_t size) { return a.allocate(size); } \
	Block allocateAll() { return a.allocateAll(); } \
	bool expand(Block &block, size_t delta) { return a.expand(block, delta); } \
	void reallocate(Block &block, size_t newSize) { a.reallocate(block, newSize); } \
	bool owns(Block block) { return a.owns(block); } \
	void deallocate(Block block) { DEALLOCATE_NULL_CHECK(block); a.deallocate(block); } \
	void deallocateAll() { a.deallocateAll(); }

template<class A>
concept ElectricFenceAllocator = A::HAS_INFINITE_FALLBACK;

/**
 * If debug mode is disabled, this just falls through to the parent.
 */
template<ElectricFenceAllocator A, template<class> class Storage = RefStorage> 
class ElectricFence : public Storage<A> {
	using Storage<A>::a;

public:
	ElectricFence(const ElectricFence &) = delete;
	ElectricFence(ElectricFence &&) = default;
#if ENABLE_DEBUG

	static constexpr bool HAS_INFINITE_FALLBACK = A::HAS_INFINITE_FALLBACK;
	static constexpr u8 alignment = A::alignment;
	static constexpr u8 dAlignment = 2 * alignment;
	static constexpr u8 startFenceByte = 0xf8;
	static constexpr u8 endFenceByte = 0xf9;

	static constexpr size_t goodSize(size_t size) { 
		return A::goodSize(size) + dAlignment; 
	}

	Block allocateAllBlock;
	std::vector<Block> *allocList; // NOTE(Erik): Intentional memory leak. At the level of memory allocators, proper move/copy semantics are not implemented for non-trivial types like std::vector . So the only way to make this work is to make the allocation list on the heap and leak the container.

	template<typename... Args>
	ElectricFence(Args&&... args) : Storage<A>(std::forward<Args>(args)...) {
		allocList = new std::vector<Block>();
		LOG_INFO("ElectricFence enabled.");
	}

	Block getBlockWithFence(Block &block) {
		return {
			.ptr = block.ptr - alignment,
			.size = block.size + dAlignment
		};
	}

	Block getBlockWithoutFence(Block &block) {
		return {
			.ptr = block.ptr + alignment,
			.size = block.size - dAlignment
		};
	}

	Block allocate(size_t size) {
		Block block = a.allocate(size + dAlignment); 
		if (block.ptr == nullptr) return block;

		memset(block.ptr, startFenceByte, alignment);
		memset(block.ptr + alignment + size, endFenceByte, alignment);
		Block blockWithoutFence = getBlockWithoutFence(block);
		allocList->push_back(blockWithoutFence);

		return std::move(blockWithoutFence);
	}

	Block allocateAll() {
		Block block = a.allocateAll();
		if (block.ptr == nullptr) return block;

		assert (block.size >= dAlignment);
		memset(block.ptr, startFenceByte, alignment);
		memset(block.ptr + block.size - alignment, endFenceByte, alignment);
		allocateAllBlock = {
			.ptr = block.ptr + alignment,
			.size = block.size - dAlignment
		};
		allocList->push_back(allocateAllBlock);

		return allocateAllBlock;
	}

	bool isBlockAllocated(Block &block) {
		return std::find(allocList->begin(), allocList->end(), block) != allocList->end();
	}

	bool isBlockAllocatedAndRemove(Block &block) {
		auto found = std::find(allocList->begin(), allocList->end(), block);
		if (found == allocList->end()) {
			return false;
		} else {
			allocList->erase(found);
			return true;
		}
	}

#define CHECK_IS_ALLOCATED(block) \
		if (!isBlockAllocated((block))) { \
			throw std::runtime_error("Block was never allocated before being used. This could indicates a use after free (or before allocate) bug"); \
		}

#define CHECK_IS_ALLOCATED_AND_REMOVE(block) \
		if (!isBlockAllocatedAndRemove((block))) { \
			throw std::runtime_error("Block was never allocated before being used. This could indicates a use after free (or before allocate) bug"); \
		}

	bool expand(Block &block, size_t delta) { 
		CHECK_IS_ALLOCATED(block);

		Block blockWithFence = getBlockWithFence(block);
		if (a.expand(blockWithFence, delta)) {
			memset(blockWithFence.ptr + blockWithFence.size - alignment, endFenceByte, alignment);

			allocList->erase(std::find(allocList->begin(), allocList->end(), block));
			block = getBlockWithoutFence(blockWithFence);
			allocList->push_back(block);
			return true;
		}
		return false;
	}

	void reallocate(Block &block, size_t newSize) { 
		CHECK_IS_ALLOCATED(block);

		Block blockWithFence = getBlockWithFence(block);
		a.reallocate(blockWithFence, newSize + dAlignment);
		memset(blockWithFence.ptr, startFenceByte, alignment);
		memset(blockWithFence.ptr + alignment + newSize, endFenceByte, alignment);

		allocList->erase(std::find(allocList->begin(), allocList->end(), block));
		block = getBlockWithoutFence(blockWithFence);
		allocList->push_back(block);
	}

	bool owns(Block block) { 
		CHECK_IS_ALLOCATED(block);
		return a.owns(block); 
	}

	bool isElectricFenceUnmodified(const Block &block) {
		return equalTo(block.ptr, alignment, startFenceByte) && equalTo(block.ptr + block.size - alignment, alignment, endFenceByte);
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		CHECK_IS_ALLOCATED_AND_REMOVE(block);

		Block blockWithFence = getBlockWithFence(block); 

		if (!isElectricFenceUnmodified(blockWithFence)) {
			throw std::runtime_error(std::format("Electric fence detected a buffer overflow at block {}, {}.", (void*)block.ptr, block.size));
		}

		a.deallocate(blockWithFence);
	}

	void deallocateAll() { 
		CHECK_IS_ALLOCATED_AND_REMOVE(allocateAllBlock);
		if (!isElectricFenceUnmodified(allocateAllBlock)) {
			throw std::runtime_error(std::format("Electric fence detected a buffer overflow at block {}, {}.", (void*)allocateAllBlock.ptr, allocateAllBlock.size));
		}

		a.deallocateAll();
	}

#else
	template<typename... Args>
	ElectricFence(Args&&... args) : Storage<A>(std::forward<Args>(args)...) { }

	FALLTHROUGH()
#endif
};

/**
 *
 * If profiler mode is disabled, this just falls through to the parent.
 */
template<class A, template<class> class Storage = RefStorage> 
class Instrument : public Storage<A> {
	using Storage<A>::a;

public:
	Instrument() {}
	Instrument(const Instrument &) = delete;
	Instrument(Instrument &&) = default;
#if ENABLE_PROFILER

	size_t callsToAllocate = 0;
	size_t callsToAllocateAll = 0;
	size_t callsToExpand = 0;
	size_t callsToReallocate = 0;
	size_t callsToOwns = 0;
	size_t callsToDeallocate = 0;
	size_t callsToDeallocateAll = 0;

	static constexpr bool HAS_INFINITE_FALLBACK = A::HAS_INFINITE_FALLBACK;
	static constexpr u8 alignment = A::alignment;

	static constexpr size_t goodSize(size_t size) { return A::goodSize(size); }

	~Instrument() {
		LOG_INFO(std::format("Allocator at {} had callsToAllocate={}", (void*)this, callsToAllocate));
		LOG_INFO(std::format("Allocator at {} had callsToAllocateAll={}", (void*)this, callsToAllocateAll));
		LOG_INFO(std::format("Allocator at {} had callsToExpand={}", (void*)this, callsToExpand));
		LOG_INFO(std::format("Allocator at {} had callsToReallocate={}", (void*)this, callsToReallocate));
		LOG_INFO(std::format("Allocator at {} had callsToOwns={}", (void*)this, callsToOwns));
		LOG_INFO(std::format("Allocator at {} had callsToDeallocate={}", (void*)this, callsToDeallocate));
		LOG_INFO(std::format("Allocator at {} had callsToDeallocateAll={}", (void*)this, callsToDeallocateAll));
	}

	Block allocate(size_t size) { 
		callsToAllocate++;
		return a.allocate(size); 
	}

	Block allocateAll() { 
		callsToAllocateAll++;
		return a.allocateAll(); 
	}

	bool expand(Block &block, size_t delta) { 
		callsToExpand++;
		return a.expand(block, delta); 
	}

	void reallocate(Block &block, size_t newSize) { 
		callsToReallocate++;
		a.reallocate(block, newSize); 
	}

	bool owns(Block block) { 
		callsToOwns++;
		return a.owns(block); 
	}

	void deallocate(Block block) {
		DEALLOCATE_NULL_CHECK(block);
		callsToDeallocate++;
		a.deallocate(block);
	}

	void deallocateAll() { 
		callsToDeallocateAll++;
		a.deallocateAll(); 
	}

#else
	FALLTHROUGH()
#endif
};

template<typename T, class A, template<class> class Storage = RefStorage>
class Typed : public Storage<A> {
	using Storage<A>::a;

public:
	template<typename... Args>
	Typed(Args&&... args) : Storage<A>(std::forward<Args>(args)...) {}

	static constexpr u8 alignment = A::alignment;

	static constexpr size_t goodSize(size_t size) {
		return A::goodSize(size);
	}

	TypedBlock<T> allocate(size_t numberOfItems) {
		return a.allocate(numberOfItems * sizeof(T));
	}

	TypedBlock<T> allocateAll() {
		return a.allocateAll();
	}

	bool expand(TypedBlock<T> &block, size_t deltaNumberOfItems) {
		Block blk = block;
		bool res = a.expand(blk, deltaNumberOfItems * sizeof(T));
		block = blk;
		return res;
	}

	void reallocate(TypedBlock<T> &block, size_t newNumberOfItems) {
		Block blk = block;
		a.reallocate(blk, newNumberOfItems * sizeof(T));
		block = blk;
	}

	bool owns(TypedBlock<T> block) {
		return a.owns(block);
	}

	void deallocate(TypedBlock<T> block) {
		a.deallocate(block);
	}

	void deallocateAll() {
		a.deallocateAll();
	}

};

// Common composites.

template<class A> using Protect = Instrument<ElectricFence<A, NoStorage>, NoStorage>;

template<size_t Size> using StaticArena = Protect<Contiguous<Size>>;

template<size_t Size> using DynamicArena = Protect<Contiguous<Size, false>>;

template<size_t Size> using StaticArenaWithMallocFallback = Fallback<Protect<StaticArena<Size>>, Protect<Malloc>>;

using Default = Protect<Malloc>;

NAMESPACE_END()

