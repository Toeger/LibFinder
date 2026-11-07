#include "iterable.h"

#include <catch2/catch_all.hpp>
#include <vector>

struct Basic_assign_next : Iterable {
	void assign_next(std::optional<int> &next) {
		if (i == 10) {
			next = std::nullopt;
		} else {
			next = i++;
		}
	}
	int i = 0;
};

struct Basic_get_next : Iterable {
	std::optional<int> get_next() {
		if (i == 10) {
			return std::nullopt;
		} else {
			return i++;
		}
	}
	int i = 0;
};

static_assert(detail::iterable::has_direct_get_next<Basic_get_next>);
static_assert(detail::iterable::has_direct_assign_next<Basic_assign_next>);
static_assert(not detail::iterable::has_direct_get_next<Basic_assign_next>);
static_assert(not detail::iterable::has_direct_assign_next<Basic_get_next>);

static_assert(not detail::iterable::has_direct_get_next<const Basic_get_next>);
static_assert(not detail::iterable::has_direct_get_next<const Basic_assign_next>);
static_assert(not detail::iterable::has_direct_assign_next<const Basic_get_next>);
static_assert(not detail::iterable::has_direct_assign_next<const Basic_assign_next>);

static_assert(not detail::iterable::Iterable_compatible<Iterable>);

const static std::vector nums{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
const static std::vector squares{0, 1, 2 * 2, 3 * 3, 4 * 4, 5 * 5, 6 * 6, 7 * 7, 8 * 8, 9 * 9};

TEST_CASE("Basic assign_next iteration", "iterable") {
	int count = 0;
	for (int &i : Basic_assign_next{}) {
		REQUIRE(count++ == i);
	}
}

TEST_CASE("Basic get_next iteration", "iterable") {
	int count = 0;
	for (int &i : Basic_get_next{}) {
		REQUIRE(count++ == i);
	}
}

TEST_CASE("Getting the generated next", "iterable") {
	std::optional<int> result = Basic_assign_next{}.get_next();
	REQUIRE(result.has_value());
	REQUIRE(*result == 0);
	Basic_get_next{}.assign_next(result);
	REQUIRE(result.has_value());
	REQUIRE(*result == 0);
}

TEST_CASE("Container conversion", "iterable") {
	std::vector<int> v = Basic_assign_next{};
	REQUIRE(v == nums);
}

TEST_CASE("Transform", "iterable") {
	std::vector<int> v = Basic_assign_next{} | [](int i) { return i * i; };
	REQUIRE(v == squares);
}

TEST_CASE("Limit", "iterable") {
	std::vector<int> v = Basic_assign_next{} | limit(3);
	REQUIRE(v == std::vector{0, 1, 2});
}

TEST_CASE("Over limit", "iterable") {
	std::vector<int> v = Basic_assign_next{} | limit(100);
	REQUIRE(v == nums);
}

TEST_CASE("Making iterable", "iterable") {
	std::vector v1 = nums;
	static_assert(is_iterable<std::vector<int> &>);
	static_assert(is_iterable<decltype(v1)>);
	static_assert(not std::is_void_v<std::iterator_traits<decltype(std::begin(v1))>::value_type>);
	auto source = v1 | iterable;
	Make_iterable<std::vector<int>> mi{v1};
	auto it = std::begin(source);
	auto it2 = std::move(it);
	it2 = std::move(it);
	static_assert(std::input_or_output_iterator<decltype(it)>);
	std::vector<int> v2{std::from_range, v1 | iterable};
	std::vector<int> v3 = v2 | iterable;
	REQUIRE(v1 == v2);
	REQUIRE(v2 == v3);
}

struct Reference_get : Iterable {
	std::optional<int &> get_next() {
		if (i == 10) {
			return std::nullopt;
		} else {
			val = i++;
			return val;
		}
	}
	int i = 0;
	int val;
};

static_assert(detail::iterable::has_direct_get_next<Reference_get>);
static_assert(std::is_same_v<detail::iterable::Product<Reference_get, &Reference_get::get_next>, int &>);
static_assert(std::is_same_v<decltype(std::begin(std::declval<Reference_get &>()))::real_value_type, int &>);

struct Reference_assign : Iterable {
	void assign_next(std::optional<int &> &next) {
		if (i == 10) {
			next = std::nullopt;
		} else {
			val = i++;
			next = val;
		}
	}
	int i = 0;
	int val;
};

static_assert(detail::iterable::has_direct_assign_next<Reference_assign>);
static_assert(std::is_same_v<detail::iterable::Product<Reference_assign, &Reference_assign::assign_next>, int &>);
static_assert(std::is_same_v<decltype(std::begin(std::declval<Reference_assign &>()))::real_value_type, int &>);

TEST_CASE("For each", "iterable") {
	std::vector v1 = nums;
	v1 | iterable | [](int &i) { i = i * i; };
	REQUIRE(v1 == squares);
}
