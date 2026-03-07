#pragma once
#include "allocators.h"

template<class A>
concept AllocatorConcept = requires(A a, Allocator::Block block, size_t size) {
	{ a.allocate(size) } -> std::same_as<Allocator::Block>;
	{ a.deallocate(block) } -> std::convertible_to<void>;
	{ a.reallocate(block, size) } -> std::convertible_to<void>;
	{ a.expand(block, size) } -> std::same_as<bool>;
};

template<AllocatorConcept A, typename T>
class DynamicArray {
	A &a;
	Allocator::TypedBlock<T> block;
	size_t used = 0;

	static size_t nextCapacity(size_t current) {
		if (current == 0) return 1;
		return current * 2;  // Exponential growth
	}

	void destroyElements(size_t start, size_t end) {
		for (size_t i = start; i < end; ++i) {
			std::destroy_at(block.ptr + i);
		}
	}

	void grow(size_t minCapacity) {
		size_t newCapacity = capacity();
		while (newCapacity < minCapacity) {
			newCapacity = nextCapacity(newCapacity);
		}
		
		// Try in-place expansion first
		size_t newBytes = newCapacity * sizeof(T);
		size_t deltaBytes = newBytes - block.size;
		if (deltaBytes > 0) {
			Allocator::Block blk = block;
			if (a.expand(blk, deltaBytes)) {
				block = blk;
				return;
			}
		}
		
		// Fall back to reallocation
		if constexpr (std::is_trivially_move_constructible_v<T> && std::is_trivially_destructible_v<T>) {
			Allocator::Block blk = block;
			a.reallocate(blk, newBytes);
			block = blk;
			return;
		}

		Allocator::Block oldBlk = block;
		Allocator::Block newBlk = a.allocate(newBytes);
		T *newPtr = reinterpret_cast<T *>(newBlk.ptr);
		std::uninitialized_move(block.ptr, block.ptr + used, newPtr);
		destroyElements(0, used);
		a.deallocate(oldBlk);
		block = newBlk;
	}

public:
	using allocator_type = A;

	DynamicArray(A &alloc, size_t initialCapacity) : a(alloc) {
		if (initialCapacity == 0) {
			LOG_ERROR("DynamicArray: initial capacity cannot be 0.");
			return;
		}
		block = a.allocate(initialCapacity * sizeof(T));
	}

	~DynamicArray() {
		if (block.ptr != nullptr) {
			destroyElements(0, used);
			a.deallocate(block);
		}
	}

	explicit DynamicArray(const DynamicArray &other) : a(other.a) {
		Allocator::Block blk = a.allocate(other.block.size);
		block = blk;
		used = other.used;
		if (used > 0 && block.ptr != nullptr) {
			std::uninitialized_copy(other.block.ptr, other.block.ptr + used, block.ptr);
		}
	}

	DynamicArray(DynamicArray &&other) noexcept : a(other.a) {
		block = other.block;
		used = other.used;
		other.block.ptr = nullptr;
		other.block.size = 0;
		other.used = 0;
	}

	DynamicArray operator=(const DynamicArray &other) noexcept {
		return DynamicArray(other);
	}

	DynamicArray &operator=(DynamicArray &&other) noexcept {
		if (this != &other) {
			// Deallocate current
			if (block.ptr != nullptr) {
				destroyElements(0, used);
				a.deallocate(block);
			}
			// Take from other
			block = other.block;
			used = other.used;
			other.block.ptr = nullptr;
			other.block.size = 0;
			other.used = 0;
		}
		return *this;
	}

	T *begin() const { return block.ptr; }
	T *end() const { return block.ptr + used; }
	const T *cbegin() const { return block.ptr; }
	const T *cend() const { return block.ptr + used; }

	size_t size() const { return used; }
	size_t capacity() const { return block.size / sizeof(T); }
	bool empty() const { return used == 0; }

	T &operator[](size_t idx) { return block.ptr[idx]; }
	const T &operator[](size_t idx) const { return block.ptr[idx]; }


	bool contains(const T& element) {
		for (size_t i = 0; i < used; ++i) {
			if (block.ptr[i] == element) {
				return true;
			}
		}
		return false;
	}

	void reserve(size_t n) {
		if (n > capacity()) {
			grow(n);
		}
	}

	void pushBack(const T &value) {
		if (used >= capacity()) {
			grow(used + 1);
		}
		::new (static_cast<void *>(block.ptr + used)) T(value);
		++used;
	}

	void pushBack(T &&value) {
		if (used >= capacity()) {
			grow(used + 1);
		}
		::new (static_cast<void *>(block.ptr + used)) T(std::move(value));
		++used;
	}

	void popBack() {
		if (used > 0) {
			--used;
			std::destroy_at(block.ptr + used);
		}
	}

	void clear() {
		destroyElements(0, used);
		used = 0;
	}



	void eraseRange(size_t startInclusive, size_t endExclusive) {
		if (startInclusive >= endExclusive || startInclusive >= used) {
			LOG_ERROR("eraseRange: startInclusive >= endExclusive || startInclusive >= used");
			return;
		}
		if (endExclusive > used) {
			LOG_ERROR("eraseRange: endExlusive > used");
			return;
		}
		
		size_t count = endExclusive - startInclusive;
		for (size_t i = endExclusive; i < used; ++i) {
			block.ptr[i - count] = std::move(block.ptr[i]);
		}

		destroyElements(used - count, used);
		used -= count;
	}

	template<class A2>
	void insertRangeAt(size_t insertPos, const DynamicArray<A2, T> &other, size_t startInclusive, size_t endExclusive) {
		if (startInclusive >= endExclusive) return;
		if (endExclusive > other.size()) endExclusive = other.size();
		if (insertPos > used) insertPos = used;
		
		size_t count = endExclusive - startInclusive;
		if (count == 0) return;
		
		// Make room
		size_t oldUsed = used;
		reserve(used + count);
		
		// Shift existing elements to the right
		for (size_t i = oldUsed; i > insertPos; --i) {
			size_t src = i - 1;
			size_t dest = src + count;
			if (dest < oldUsed) {
				block.ptr[dest] = std::move(block.ptr[src]);
			} else {
				::new (static_cast<void *>(block.ptr + dest)) T(std::move(block.ptr[src]));
			}
		}

		// TODO: Incomplete.
		if constexpr (std::is_same_v<typeof(*this), typeof(other)>) {
			if (this == &other) {
				if (insertPos >= endExclusive) {
					// Nothing to do here.
				} else if (insertPos < startInclusive) {
					startInclusive += count;
					endExclusive += count;
				} else {
					LOG_ERROR("insertRangeAt: Overlapping sections are not supported.");
					return;
				}
			}
		}
		
		// Copy from other
		for (size_t i = 0; i < count; ++i) {
			size_t dest = insertPos + i;
			if (dest < oldUsed) {
				block.ptr[dest] = other.begin()[startInclusive + i];
			} else {
				::new (static_cast<void *>(block.ptr + dest)) T(other.begin()[startInclusive + i]);
			}
		}

		// TODO: Incomplete.
		if constexpr (std::is_same_v<typeof(*this), typeof(other)>) {
			if (this == &other) {
				if (insertPos >= endExclusive) {
					eraseRange(startInclusive + count, endExclusive + count);
				} else if (insertPos < startInclusive) {
					eraseRange(startInclusive, endExclusive);
				}
			}
		}
		
		used = oldUsed + count;
	}

	void insertRange(const DynamicArray<A, T> &other, size_t startInclusive, size_t endExclusive) {
		insertRangeAt(used, other, startInclusive, endExclusive);
	}
};


