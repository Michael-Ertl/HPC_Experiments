#include "allocators.h"
#include "array.h"
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

template<class A>
struct Nested {
	DynamicArray<A, int> array;
};

template<class A>
struct Structure {
	A allocator;
	DynamicArray<A, Nested<A>> *nested;
	using allocator_type = A;
};

using StaticContiguous = Allocator::ElectricFence<Allocator::Fallback<Allocator::Freelist<Allocator::Contiguous<1024, true>, 1, 128, Allocator::NoStorage>, Allocator::Malloc>>;

using Structures = std::variant<
	Structure<Allocator::Malloc>,
	Structure<StaticContiguous>
>;
using StructureParam = NamedFactory<Structures>;
class DynamicArrayTest : public testing::TestWithParam<StructureParam> {};

TEST_P(DynamicArrayTest, PushBackLValue) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			structure.nested->pushBack(nested);

			EXPECT_EQ(structure.nested->capacity(), 16);
			EXPECT_EQ(structure.nested->size(), 1);
			EXPECT_EQ((*structure.nested)[0].array.capacity(), 17);
			EXPECT_EQ((*structure.nested)[0].array.size(), 0);
	}, instance);
}

TEST_P(DynamicArrayTest, PushBackRValue) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			structure.nested->pushBack(std::move(nested));

			EXPECT_EQ(structure.nested->capacity(), 16);
			EXPECT_EQ(structure.nested->size(), 1);
			EXPECT_EQ((*structure.nested)[0].array.capacity(), 17);
			EXPECT_EQ((*structure.nested)[0].array.size(), 0);
	}, instance);
}

TEST_P(DynamicArrayTest, PopBack) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			structure.nested->pushBack(nested);
			structure.nested->popBack();

			EXPECT_EQ(structure.nested->capacity(), 16);
			EXPECT_EQ(structure.nested->size(), 0);
	}, instance);
}

TEST_P(DynamicArrayTest, Clear) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			structure.nested->pushBack(Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			});
			structure.nested->pushBack(Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			});
			structure.nested->pushBack(Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			});

			structure.nested->clear();

			EXPECT_EQ(structure.nested->capacity(), 16);
			EXPECT_EQ(structure.nested->size(), 0);
	}, instance);
}

TEST_P(DynamicArrayTest, CopyArray) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			auto copy = nested;

			structure.nested->pushBack(std::move(nested));
			structure.nested->pushBack(std::move(copy));

			(*structure.nested)[0].array.pushBack(0);

			EXPECT_EQ(structure.nested->capacity(), 16);
			EXPECT_EQ(structure.nested->size(), 2);
			EXPECT_EQ((*structure.nested)[0].array.size(), 1);
			EXPECT_EQ((*structure.nested)[1].array.size(), 0);
	}, instance);
}

TEST_P(DynamicArrayTest, MoveArray) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;
			auto nested = Nested<A>{
				.array = DynamicArray<A, int>(structure.allocator, 17)
			};
			auto moved = Nested<A>{
				.array = std::move(nested.array)
			};

			moved.array.pushBack(0);

			EXPECT_EQ(moved.array.size(), 1);
	}, instance);
}

TEST_P(DynamicArrayTest, EraseRange) {
	auto instance = GetParam().factory();
	std::visit([](auto &structure) {
			using S = std::decay_t<decltype(structure)>;
			using A = S::allocator_type;

			for (size_t _ = 0; _ < 10; _++) {
				structure.nested->pushBack(Nested<A>{
					.array = DynamicArray<A, int>(structure.allocator, 17)
				});
			}

			structure.nested->eraseRange(0, 5);

			EXPECT_EQ(structure.nested->size(), 5);
	}, instance);
}

INSTANTIATE_TEST_SUITE_P(Core, DynamicArrayTest, testing::Values(
	StructureParam{"Malloc", []() -> Structures { 
		using A = Allocator::Malloc;
		Structure<A> structure = {
			.allocator = A(),
			.nested = nullptr
		};
		structure.nested = new DynamicArray<A, Nested<A>>(structure.allocator, 16);
		return structure;
	}},
	StructureParam{"StaticContiguous",  []() -> Structures { 
		using A = StaticContiguous;

		using A1 = Allocator::Freelist<Allocator::Contiguous<1024, true>, 1, 128, Allocator::NoStorage>;
		using A2 = Allocator::Malloc;

		A1 *a1 = new A1();
		A2 *a2 = new A2();
		auto *fallback = new Allocator::Fallback(*a1, *a2);

		Structure<A> structure = {
			.allocator = A(*fallback),
			.nested = nullptr
		};
		structure.nested = new DynamicArray<A, Nested<A>>(structure.allocator, 16);
		return structure;
	}}
), NameGenerator());

