#include "allocators.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <span>

// ======================================================================
// Named parameterized test support
// ======================================================================

template<typename Variant>
struct NamedFactory {
	std::string name;
	std::function<Variant()> factory;
};

struct NameGenerator {
	template<typename T>
	std::string operator()(const testing::TestParamInfo<T> &info) const {
		return info.param.name;
	}
};

// ======================================================================
// FailureStub
// ======================================================================

TEST(FailureStub, AllMethodsReturnFailure) {
	Allocator::FailureStub alloc;
	Allocator::Block block = {};

	EXPECT_EQ(alloc.goodSize(10), 0);
	EXPECT_EQ(alloc.allocate(10).ptr, nullptr);
	EXPECT_EQ(alloc.allocateAll().ptr, nullptr);
	EXPECT_FALSE(alloc.expand(block, 10));
	EXPECT_FALSE(alloc.owns({}));
}

// ======================================================================
// Malloc-specific
// ======================================================================

TEST(Malloc, ReallocatePreservesData) {
	Allocator::Malloc alloc;
	Allocator::Block block = alloc.allocate(64);
	ASSERT_NE(block.ptr, nullptr);
	memset(block.ptr, 0xAB, 64);
	alloc.reallocate(block, 256);
	EXPECT_EQ(block.size, 256);
	EXPECT_THAT(std::span(block.ptr, 64), ::testing::Each(0xAB));
	alloc.deallocate(block);
}

// ======================================================================
// Type aliases
// ======================================================================

using StaticContiguous = Allocator::Contiguous<1024, true>;
using DynamicContiguous = Allocator::Contiguous<1024, false>;
using MallocFreelist    = Allocator::Freelist<Allocator::Malloc, 16, 64, Allocator::NoStorage>;
using ContiguousFreelist = Allocator::Freelist<Allocator::Contiguous<4096>, 16, 64, Allocator::NoStorage>;
using HighMinFreelist   = Allocator::Freelist<Allocator::Malloc, 32, 64, Allocator::NoStorage>; // MinAllocSize > alignment

// ======================================================================
// goodSize — alignment rounding
// ======================================================================

TEST(GoodSize, MallocRoundsUpToMaxAlign) {
	const size_t al = Allocator::Malloc::alignment;
	EXPECT_EQ(Allocator::Malloc::goodSize(1),    al);
	EXPECT_EQ(Allocator::Malloc::goodSize(al),   al);
	EXPECT_EQ(Allocator::Malloc::goodSize(al+1), al * 2);
}

TEST(GoodSize, ContiguousRoundsUpToMaxAlign) {
	const size_t al = StaticContiguous::alignment;
	EXPECT_EQ(StaticContiguous::goodSize(1),    al);
	EXPECT_EQ(StaticContiguous::goodSize(al),   al);
	EXPECT_EQ(StaticContiguous::goodSize(al+1), al * 2);
}

// ======================================================================
// Parameterized: Core allocators — roundtrip + alignment
// ======================================================================

using CoreAllocators = std::variant<
	Allocator::Malloc,
	StaticContiguous,
	DynamicContiguous
>;
using CoreParam = NamedFactory<CoreAllocators>;
class CoreAllocatorTest : public testing::TestWithParam<CoreParam> {};

TEST_P(CoreAllocatorTest, AllocateDeallocateRoundtrip) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		Allocator::Block block = alloc.allocate(64);
		ASSERT_NE(block.ptr, nullptr);
		memset(block.ptr, 0xAB, block.size);
		EXPECT_THAT(std::span(block.ptr, block.size), ::testing::Each(0xAB));
		alloc.deallocate(block);
	}, instance);
}

TEST_P(CoreAllocatorTest, AlignmentWorks) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto b0 = alloc.allocate(1);
		ASSERT_NE(b0.ptr, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(b0.ptr) % alloc.alignment, 0);
		auto b1 = alloc.allocate(3);
		ASSERT_NE(b1.ptr, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(b1.ptr) % alloc.alignment, 0);
		auto b2 = alloc.allocate(5);
		ASSERT_NE(b2.ptr, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(b2.ptr) % alloc.alignment, 0);
		auto b3 = alloc.allocate(1);
		ASSERT_NE(b3.ptr, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(b3.ptr) % alloc.alignment, 0);
	}, instance);
}

INSTANTIATE_TEST_SUITE_P(Core, CoreAllocatorTest, testing::Values(
	CoreParam{"Malloc",            []() -> CoreAllocators { return Allocator::Malloc(); }},
	CoreParam{"StaticContiguous",  []() -> CoreAllocators { return StaticContiguous(); }},
	CoreParam{"DynamicContiguous", []() -> CoreAllocators { return DynamicContiguous(); }}
), NameGenerator());

// ======================================================================
// Parameterized: Bounded allocators — OOM + ownership
// ======================================================================

using BoundedAllocators = std::variant<
	StaticContiguous,
	DynamicContiguous
>;
using BoundedParam = NamedFactory<BoundedAllocators>;
class BoundedAllocatorTest : public testing::TestWithParam<BoundedParam> {};

TEST_P(BoundedAllocatorTest, AllocateReturnsNullOnOOM) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		EXPECT_EQ(alloc.allocate(2048).ptr, nullptr);
	}, instance);
}

TEST_P(BoundedAllocatorTest, OwnsAllocatedBlock) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		Allocator::Block block = alloc.allocate(32);
		ASSERT_NE(block.ptr, nullptr);
		EXPECT_TRUE(alloc.owns(block));
	}, instance);
}

TEST_P(BoundedAllocatorTest, DoesNotOwnForeignPointer) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		u8 stackVar = 0;
		Allocator::Block foreign = { &stackVar, 1 };
		EXPECT_FALSE(alloc.owns(foreign));
	}, instance);
}

TEST_P(BoundedAllocatorTest, LIFODeallocation) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto b1 = alloc.allocate(32);
		ASSERT_NE(b1.ptr, nullptr);
		auto b2 = alloc.allocate(32);
		ASSERT_NE(b2.ptr, nullptr);
		alloc.deallocate(b2);
		auto b3 = alloc.allocate(32);
		ASSERT_NE(b3.ptr, nullptr);
		EXPECT_EQ(b2.ptr, b3.ptr);
	}, instance);
}

TEST_P(BoundedAllocatorTest, DeallocateAllResetsCapacity) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto b1 = alloc.allocate(512);
		ASSERT_NE(b1.ptr, nullptr);
		alloc.allocate(512);
		EXPECT_EQ(alloc.allocate(1).ptr, nullptr);
		alloc.deallocateAll();
		auto b2 = alloc.allocate(512);
		ASSERT_NE(b2.ptr, nullptr);
		EXPECT_EQ(b1.ptr, b2.ptr);
	}, instance);
}

TEST_P(BoundedAllocatorTest, ExpandLastBlockSucceeds) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto block = alloc.allocate(32);
		ASSERT_NE(block.ptr, nullptr);
		u8 *const original_ptr = block.ptr;
		EXPECT_TRUE(alloc.expand(block, 16));
		EXPECT_EQ(block.size, 48);
		EXPECT_EQ(block.ptr, original_ptr);
	}, instance);
}

TEST_P(BoundedAllocatorTest, ExpandUpdatesUsedSoNextAllocateDoesNotOverlap) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto b1 = alloc.allocate(32);
		ASSERT_NE(b1.ptr, nullptr);
		EXPECT_TRUE(alloc.expand(b1, 16));
		EXPECT_EQ(b1.size, 48);

		// Next allocation must not overlap the expanded region.
		auto b2 = alloc.allocate(16);
		ASSERT_NE(b2.ptr, nullptr);
		EXPECT_GE(b2.ptr, b1.ptr + b1.size);
	}, instance);
}

TEST_P(BoundedAllocatorTest, DeallocateUnalignedSizeReclaims) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		// Allocate a non-aligned size (3 bytes → goodSize rounds to alignment).
		auto b1 = alloc.allocate(3);
		ASSERT_NE(b1.ptr, nullptr);
		alloc.deallocate(b1);

		// LIFO: the slot should be reclaimed and reused.
		auto b2 = alloc.allocate(3);
		ASSERT_NE(b2.ptr, nullptr);
		EXPECT_EQ(b1.ptr, b2.ptr);
	}, instance);
}

TEST_P(BoundedAllocatorTest, ExpandNonLastBlockFails) {
	auto instance = GetParam().factory();
	std::visit([](auto &alloc) {
		auto b1 = alloc.allocate(32);
		ASSERT_NE(b1.ptr, nullptr);
		alloc.allocate(32);
		EXPECT_FALSE(alloc.expand(b1, 16));
		EXPECT_EQ(b1.size, 32);
	}, instance);
}

INSTANTIATE_TEST_SUITE_P(Bounded, BoundedAllocatorTest, testing::Values(
	BoundedParam{"StaticContiguous",  []() -> BoundedAllocators { return StaticContiguous(); }},
	BoundedParam{"DynamicContiguous", []() -> BoundedAllocators { return DynamicContiguous(); }}
), NameGenerator());

// ======================================================================
// Freelist-specific
// ======================================================================

TEST(Freelist, RecyclesFreedBlock) {
	MallocFreelist alloc;
	auto b1 = alloc.allocate(16);
	ASSERT_NE(b1.ptr, nullptr);
	alloc.deallocate(b1);
	auto b2 = alloc.allocate(16);
	ASSERT_NE(b2.ptr, nullptr);
	EXPECT_EQ(b1.ptr, b2.ptr);
}

TEST(Freelist, FallthroughToParentForOversizedBlock) {
	MallocFreelist alloc;
	// 128 > MaxAllocSize(64), should fall through to Malloc
	auto block = alloc.allocate(128);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_EQ(block.size, 128);
}

TEST(Freelist, ExpandWithinMaxSucceeds) {
	MallocFreelist alloc;
	auto block = alloc.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_TRUE(alloc.expand(block, 16)); // 16 + 16 = 32 <= 64
	EXPECT_EQ(block.size, 32);
}

TEST(Freelist, ExpandBeyondMaxFails) {
	MallocFreelist alloc;
	auto block = alloc.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_FALSE(alloc.expand(block, 128)); // 16 + 128 = 144 > 64
	EXPECT_EQ(block.size, 16);
}

TEST(Freelist, OwnsRecognizesSubMinRawSize) {
	// Use ContiguousFreelist: Contiguous::owns does a real pointer-range check,
	// so Malloc::owns (always true) can't mask the bug.
	ContiguousFreelist alloc;
	auto b = alloc.allocate(8);
	ASSERT_NE(b.ptr, nullptr);
	// Deallocate and give the block a foreign pointer — only size should matter.
	u8 stackVar = 0;
	Allocator::Block foreign = { &stackVar, 8 };
	// goodSize(8) = 16 = MinAllocSize → freelist should claim ownership by size.
	// BUG: isBlockOwnedByFreelist(8) = (16 <= 8) = false, and Contiguous won't own a stack pointer.
	EXPECT_TRUE(alloc.owns(foreign));
}

TEST(Freelist, DeallocateRecyclesSubMinRawSize) {
	MallocFreelist alloc;
	auto b1 = alloc.allocate(8);
	ASSERT_NE(b1.ptr, nullptr);
	alloc.deallocate(b1);
	// Should be pushed onto freelist and recycled, not freed to parent.
	auto b2 = alloc.allocate(8);
	ASSERT_NE(b2.ptr, nullptr);
	EXPECT_EQ(b1.ptr, b2.ptr);
}

TEST(Freelist, ExpandSubMinRawSizeWithinMax) {
	MallocFreelist alloc;
	auto block = alloc.allocate(8);
	ASSERT_NE(block.ptr, nullptr);
	u8 *const original_ptr = block.ptr;
	// Block was allocated through freelist (goodSize(8)=16).
	// Expanding 8+8=16 → goodSize(16)=16 <= MaxAllocSize. Should succeed.
	EXPECT_TRUE(alloc.expand(block, 8));
	EXPECT_EQ(block.size, 16);
	EXPECT_EQ(block.ptr, original_ptr);
}

TEST(Freelist, ReallocateSubMinRawSizeStaysInFreelist) {
	MallocFreelist alloc;
	auto block = alloc.allocate(8);
	ASSERT_NE(block.ptr, nullptr);
	u8 *const original_ptr = block.ptr;
	// Block was allocated through freelist (goodSize(8) = 16).
	// Reallocate to 32 (still in range) — should resize in-place.
	// BUG: isBlockOwnedByFreelist(8) = false → falls to Malloc::reallocate
	// which calls realloc() and may return a different pointer.
	alloc.reallocate(block, 32);
	EXPECT_EQ(block.size, 32);
	EXPECT_EQ(block.ptr, original_ptr);

	// Verify the slot is still recyclable through the freelist.
	alloc.deallocate(block);
	auto b2 = alloc.allocate(32);
	EXPECT_EQ(b2.ptr, original_ptr);
}

TEST(Freelist, FallthroughToParentForUndersizedBlock) {
	HighMinFreelist alloc;
	// goodSize(16) = 16 < MinAllocSize(32), falls through to Malloc
	auto block = alloc.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_EQ(block.size, 16);
	alloc.deallocate(block);
}

TEST(Freelist, OwnsIsDeterminedBySizeNotPointer) {
	MallocFreelist alloc;
	u8 stackVar = 0;
	// A block with size in [MinAllocSize, MaxAllocSize] is considered "owned"
	// regardless of where the pointer actually came from.
	Allocator::Block external = { &stackVar, 32 };
	EXPECT_TRUE(alloc.owns(external));
}

TEST(Freelist, DeallocateAllResetsFreelist) {
	ContiguousFreelist alloc;
	auto b1 = alloc.allocate(16);
	ASSERT_NE(b1.ptr, nullptr);
	alloc.deallocateAll();
	auto b2 = alloc.allocate(16);
	ASSERT_NE(b2.ptr, nullptr);
	EXPECT_EQ(b1.ptr, b2.ptr); // bump allocator reset, same address
}

TEST(Freelist, ReallocateWithinMaxResizesInPlace) {
	MallocFreelist alloc;
	auto block = alloc.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	u8 *const original_ptr = block.ptr;
	alloc.reallocate(block, 32); // 32 <= MaxAllocSize(64), stays in freelist slot
	EXPECT_EQ(block.size, 32);
	EXPECT_EQ(block.ptr, original_ptr);
}

TEST(Freelist, ReallocateBeyondMaxFallsThrough) {
	MallocFreelist alloc;
	auto block = alloc.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	alloc.reallocate(block, 128); // 128 > MaxAllocSize(64), moves to parent
	EXPECT_EQ(block.size, 128);
	EXPECT_NE(block.ptr, nullptr);
	alloc.deallocate(block);
}

// ======================================================================
// Fallback-specific
// ======================================================================

TEST(Fallback, OwnsBlockFromPrimary) {
	StaticContiguous primary;
	Allocator::Malloc secondary;
	Allocator::Fallback fallback(primary, secondary);

	auto block = fallback.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_TRUE(primary.owns(block));
	EXPECT_TRUE(fallback.owns(block));
}

TEST(Fallback, OwnsBlockFromSecondary) {
	StaticContiguous primary;
	Allocator::Malloc secondary;
	Allocator::Fallback fallback(primary, secondary);

	while (primary.allocate(16).ptr != nullptr) {}

	auto block = fallback.allocate(16);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_FALSE(primary.owns(block));
	EXPECT_TRUE(fallback.owns(block));
}

TEST(Fallback, DeallocatePrimaryOwnedBlockReturnsToPrimary) {
	StaticContiguous primary;
	Allocator::Malloc secondary;
	Allocator::Fallback fallback(primary, secondary);

	auto b1 = fallback.allocate(32);
	ASSERT_NE(b1.ptr, nullptr);
	EXPECT_TRUE(primary.owns(b1));

	fallback.deallocate(b1);

	auto b2 = primary.allocate(32);
	EXPECT_EQ(b1.ptr, b2.ptr);
}

TEST(Fallback, ExpandPropagatesDelta) {
	StaticContiguous primary;
	DynamicContiguous secondary;
	Allocator::Fallback<StaticContiguous, DynamicContiguous> fallback(primary, secondary);

	auto block = fallback.allocate(32);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_TRUE(fallback.expand(block, 16));
	EXPECT_EQ(block.size, 48);
}

// ======================================================================
// Segregator-specific
// ======================================================================

TEST(Segregator, RoutesSmallToSmallAllocator) {
	Allocator::Contiguous<256> small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(8);
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_TRUE(small.owns(b));
}

TEST(Segregator, RoutesLargeToLargeAllocator) {
	Allocator::Contiguous<256> small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_FALSE(small.owns(b));
}

TEST(Segregator, DeallocateSmallGoesToSmallAllocator) {
	Allocator::Contiguous<256> small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(16); // 16 == threshold, routes to small
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_TRUE(small.owns(b));
	seg.deallocate(b);

	// LIFO: small allocator should recover the slot
	auto b2 = small.allocate(16);
	EXPECT_EQ(b.ptr, b2.ptr);
}

TEST(Segregator, DeallocateLargeGoesToLargeAllocator) {
	Allocator::Contiguous<256> small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(32); // 32 > threshold, routes to large
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_FALSE(small.owns(b));
	seg.deallocate(b);
}

TEST(Segregator, ReallocateIsImplemented) {
	Allocator::Malloc small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Malloc, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(8);
	ASSERT_NE(b.ptr, nullptr);
	memset(b.ptr, 0xAB, 8);

	seg.reallocate(b, 16);
	EXPECT_EQ(b.size, 16);
}

TEST(Segregator, ReallocateSmallToLargeCrossesThreshold) {
	Allocator::Malloc small;
	Allocator::Malloc large;
	Allocator::Segregator<16, Allocator::Malloc, Allocator::Malloc> seg(small, large);

	auto b = seg.allocate(8); // <= threshold, goes to small
	ASSERT_NE(b.ptr, nullptr);
	memset(b.ptr, 0xCD, 8);

	// Reallocate to size > threshold: small→large transition.
	// BUG: falls into else branch which calls l.reallocate on a block owned by small.
	seg.reallocate(b, 32);
	EXPECT_EQ(b.size, 32);
	EXPECT_THAT(std::span(b.ptr, 8), ::testing::Each(0xCD));
}

TEST(Segregator, ExpandSmallBlockAcrossThreshold) {
	Allocator::Contiguous<256> small;
	Allocator::Contiguous<1024> large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Contiguous<1024>> seg(small, large);

	auto b = seg.allocate(8); // size 8 < threshold 16, goes to small
	ASSERT_NE(b.ptr, nullptr);
	// Expanding by 16: 8 + 16 = 24 > threshold — crosses the small→large boundary.
	// Hits the TODO branch: no return statement → undefined behaviour.
	EXPECT_FALSE(seg.expand(b, 16));
}

TEST(Segregator, ExpandSmallBlock) {
	Allocator::Contiguous<256> small;
	Allocator::Contiguous<1024> large;
	Allocator::Segregator<16, Allocator::Contiguous<256>, Allocator::Contiguous<1024>> seg(small, large);

	auto b = seg.allocate(8);
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_TRUE(seg.expand(b, 8));
	EXPECT_EQ(b.size, 16);
}

// ======================================================================
// Contiguous — move semantics
// ======================================================================

TEST(Contiguous, MoveConstructorTransfersOwnership) {
	// Only meaningful for dynamic mode: static mode can't truly transfer pointer ownership.
	DynamicContiguous source;
	auto b = source.allocate(32);
	ASSERT_NE(b.ptr, nullptr);

	DynamicContiguous dest(std::move(source));
	EXPECT_TRUE(dest.owns(b));
	EXPECT_FALSE(source.owns(b)); // source cleared after move
}

// ======================================================================
// ElectricFence-specific
// ======================================================================

TEST(ElectricFence, AllocateDeallocateRoundtrip) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;

	auto block = ef.allocate(32);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_EQ(block.size, 32);
	memset(block.ptr, 0xAB, 32);
	ef.deallocate(block);
}

TEST(ElectricFence, GoodSizeIncludesFenceOverhead) {
	constexpr size_t al = Allocator::Malloc::alignment;
	// ElectricFence adds 2 * alignment bytes of fencing.
	using EF = Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage>;
	size_t efGoodSize = EF::goodSize(1);
	size_t expected = Allocator::Malloc::goodSize(1) + 2 * al;
	EXPECT_EQ(efGoodSize, expected);
}

TEST(ElectricFence, FenceBytesAreWrittenAroundBlock) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;
	constexpr size_t al = decltype(ef)::alignment;

	auto block = ef.allocate(32);
	ASSERT_NE(block.ptr, nullptr);

	// Fence bytes should be placed before and after the user region.
	u8 *startFence = block.ptr - al;
	u8 *endFence = block.ptr + block.size;
	EXPECT_TRUE(equalTo(startFence, al, decltype(ef)::startFenceByte));
	EXPECT_TRUE(equalTo(endFence, al, decltype(ef)::endFenceByte));
}

TEST(ElectricFence, WritingWithinBoundsDoesNotCorruptFence) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;
	constexpr size_t al = decltype(ef)::alignment;

	auto block = ef.allocate(32);
	ASSERT_NE(block.ptr, nullptr);

	// Fill the entire user region — fences should remain intact.
	memset(block.ptr, 0xFF, 32);
	u8 *startFence = block.ptr - al;
	u8 *endFence = block.ptr + block.size;
	EXPECT_TRUE(equalTo(startFence, al, decltype(ef)::startFenceByte));
	EXPECT_TRUE(equalTo(endFence, al, decltype(ef)::endFenceByte));
}

TEST(ElectricFence, ConsecutiveAllocationsDoNotOverlap) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;

	auto b1 = ef.allocate(32);
	auto b2 = ef.allocate(32);
	ASSERT_NE(b1.ptr, nullptr);
	ASSERT_NE(b2.ptr, nullptr);

	// User regions should not overlap (accounting for fence overhead).
	EXPECT_GE(b2.ptr, b1.ptr + b1.size);
}

TEST(ElectricFence, PreventsUseAfterFree) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;

	auto b = ef.allocate(32);
	ASSERT_NE(b.ptr, nullptr);

	testing::internal::CaptureStdout();
	ef.deallocate(b);
	ef.expand(b, 64);
	std::string output = testing::internal::GetCapturedStdout();
	ASSERT_TRUE(output.find("use after free") != std::string::npos);
}

TEST(ElectricFence, PreventsDoubleFree) {
	Allocator::ElectricFence<Allocator::Malloc, Allocator::NoStorage> ef;

	auto b = ef.allocate(32);
	ASSERT_NE(b.ptr, nullptr);

	testing::internal::CaptureStdout();
	ef.deallocate(b);
	ef.deallocate(b);
	std::string output = testing::internal::GetCapturedStdout();
	ASSERT_TRUE(output.find("use after free") != std::string::npos);
}

// ======================================================================
// Instrument-specific
// ======================================================================

TEST(Instrument, CountsAllocateAndDeallocate) {
	Allocator::Instrument<Allocator::Malloc, Allocator::NoStorage> inst;

	auto b = inst.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	inst.deallocate(b);

	EXPECT_EQ(inst.callsToAllocate, 1);
	EXPECT_EQ(inst.callsToDeallocate, 1);
}

TEST(Instrument, CountsExpand) {
	Allocator::Instrument<Allocator::Contiguous<1024>, Allocator::NoStorage> inst;

	auto b = inst.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	inst.expand(b, 16);

	EXPECT_EQ(inst.callsToExpand, 1);
}

TEST(Instrument, CountsReallocate) {
	Allocator::Instrument<Allocator::Malloc, Allocator::NoStorage> inst;

	auto b = inst.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	inst.reallocate(b, 64);
	inst.deallocate(b);

	EXPECT_EQ(inst.callsToReallocate, 1);
}

TEST(Instrument, CountsOwns) {
	Allocator::Instrument<Allocator::Malloc, Allocator::NoStorage> inst;

	auto b = inst.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	inst.owns(b);

	EXPECT_EQ(inst.callsToOwns, 1);
	inst.deallocate(b);
}

TEST(Instrument, CountsDeallocateAll) {
	Allocator::Instrument<Allocator::Contiguous<1024>, Allocator::NoStorage> inst;

	inst.allocate(32);
	inst.deallocateAll();

	EXPECT_EQ(inst.callsToDeallocateAll, 1);
}

TEST(Instrument, PassesThroughToParent) {
	Allocator::Instrument<Allocator::Contiguous<1024>, Allocator::NoStorage> inst;

	auto b = inst.allocate(32);
	ASSERT_NE(b.ptr, nullptr);
	EXPECT_EQ(b.size, 32);

	// Should behave like the parent: expand works, write/read works.
	memset(b.ptr, 0xAB, 32);
	EXPECT_TRUE(inst.expand(b, 16));
	EXPECT_EQ(b.size, 48);
	EXPECT_THAT(std::span(b.ptr, 32), ::testing::Each(0xAB));
}

// ======================================================================
// Typed-specific
// ======================================================================

TEST(Typed, AllocateReturnsTypedPointer) {
	Allocator::Typed<u32, Allocator::Malloc, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(4); // 4 u32s = 16 bytes
	ASSERT_NE(block.ptr, nullptr);
	// Should be able to write u32 values through the typed pointer.
	block.ptr[0] = 42;
	block.ptr[1] = 100;
	block.ptr[2] = 200;
	block.ptr[3] = 300;
	EXPECT_EQ(block.ptr[0], 42);
	EXPECT_EQ(block.ptr[3], 300);
	alloc.deallocate(block);
}

TEST(Typed, SizeIsInBytes) {
	Allocator::Typed<u32, Allocator::Malloc, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(4); // 4 items
	ASSERT_NE(block.ptr, nullptr);
	// block.size should reflect bytes (4 * sizeof(u32) = 16), not item count.
	EXPECT_EQ(block.size, 4 * sizeof(u32));
	alloc.deallocate(block);
}

TEST(Typed, ExpandIncreasesCapacity) {
	Allocator::Typed<u32, Allocator::Contiguous<4096>, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(4); // 4 u32s
	ASSERT_NE(block.ptr, nullptr);
	block.ptr[0] = 42;

	EXPECT_TRUE(alloc.expand(block, 2)); // expand by 2 u32s
	EXPECT_EQ(block.size, 6 * sizeof(u32));
	EXPECT_EQ(block.ptr[0], 42); // original data preserved
	block.ptr[5] = 99;
	EXPECT_EQ(block.ptr[5], 99);
}

TEST(Typed, DeallocateAllResetsAllocator) {
	Allocator::Typed<u32, Allocator::Contiguous<4096>, Allocator::NoStorage> alloc;

	auto b1 = alloc.allocate(4);
	ASSERT_NE(b1.ptr, nullptr);
	alloc.deallocateAll();

	auto b2 = alloc.allocate(4);
	ASSERT_NE(b2.ptr, nullptr);
	EXPECT_EQ((u8*)b1.ptr, (u8*)b2.ptr);
}

TEST(Typed, OwnsAllocatedBlock) {
	Allocator::Typed<u32, Allocator::Contiguous<4096>, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(4);
	ASSERT_NE(block.ptr, nullptr);
	EXPECT_TRUE(alloc.owns(block));
}

TEST(Typed, FailsOOM) {
	Allocator::Typed<u32, Allocator::Contiguous<4096>, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(6000);
	ASSERT_EQ(block.ptr, nullptr);
}

TEST(Typed, ReallocateGrowsBlock) {
	Allocator::Typed<u32, Allocator::Malloc, Allocator::NoStorage> alloc;

	auto block = alloc.allocate(4); // 4 u32s
	ASSERT_NE(block.ptr, nullptr);
	block.ptr[0] = 42;
	block.ptr[3] = 99;

	alloc.reallocate(block, 8); // grow to 8 u32s
	EXPECT_EQ(block.size, 8 * sizeof(u32));
	EXPECT_EQ(block.ptr[0], 42);
	EXPECT_EQ(block.ptr[3], 99);
	alloc.deallocate(block);
}
