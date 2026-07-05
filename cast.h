#pragma once

#include <ostream>
#include <stdexcept>
#include <type_traits>

template <class Internal>
	requires(std::is_arithmetic_v<Internal>)
struct Cast {
	static void make_sure(bool condition) {
		if (not condition) {
			throw std::runtime_error{""};
		}
	}
	template <class From>
		requires(std::is_same_v<From, Internal>)
	Cast(From v)
		: value{v} {}

	template <class From>
		requires(not std::is_same_v<From, Internal>)
	Cast(From v)
		: value{::Cast{v}} {}

	template <class To>
		requires(std::is_arithmetic_v<To>)
	operator To() {
		if constexpr (std::is_signed_v<Internal> and std::is_unsigned_v<To>) {
			make_sure(value >= 0);
			make_sure(static_cast<std::make_unsigned_t<decltype(value)>>(value) <= std::numeric_limits<To>::max());
		} else if constexpr (std::is_unsigned_v<Internal> and std::is_signed_v<To>) {
			make_sure(value <= std::numeric_limits<To>::max());
		} else {
			make_sure(std::numeric_limits<To>::min() <= value and std::numeric_limits<To>::max() >= value);
		}
		return static_cast<To>(value);
	}

	auto operator+() const {
		return Cast<decltype(+value)>(+value);
	}

	Internal value;
};

template <class T>
Cast(T) -> Cast<T>;

#define X(OP)                                                                                                                                                  \
	template <class T, class U>                                                                                                                                \
	auto operator OP(Cast<T> lhs, U rhs) {                                                                                                                     \
		using Common_Type = std::common_type_t<T, U>;                                                                                                          \
		return Cast{static_cast<Common_Type>(lhs) OP static_cast<Common_Type>(rhs)};                                                                           \
	}                                                                                                                                                          \
	template <class T, class U>                                                                                                                                \
	auto operator OP(T lhs, Cast<U> rhs) {                                                                                                                     \
		using Common_Type = std::common_type_t<T, U>;                                                                                                          \
		return Cast{static_cast<Common_Type>(lhs) OP static_cast<Common_Type>(rhs)};                                                                           \
	}                                                                                                                                                          \
	template <class T, class U>                                                                                                                                \
	auto operator OP(Cast<T> lhs, Cast<U> rhs) {                                                                                                               \
		using Common_Type = std::common_type_t<T, U>;                                                                                                          \
		return Cast{static_cast<Common_Type>(lhs) OP static_cast<Common_Type>(rhs)};                                                                           \
	}
X(+)
X(-)
X(*)
X(/)
#undef X
#define X(OP)                                                                                                                                                  \
	template <class T, class U>                                                                                                                                \
	auto operator OP(Cast<T> lhs, U rhs) {                                                                                                                     \
		using Common_Type = std::common_type_t<T, U>;                                                                                                          \
		return static_cast<Common_Type>(lhs) OP static_cast<Common_Type>(rhs);                                                                                 \
	}
X(<)
X(>)
X(<=)
X(>=)
X(==)
X(!=)
X(<=>)
#undef X

template <class T>
std::ostream &operator<<(std::ostream &os, Cast<T> v) {
	return os << v.value;
}

template <class T, class Char_t>
struct std::formatter<Cast<T>, Char_t> : std::formatter<T, Char_t> {
	template <class FmtContext>
	FmtContext::iterator format(Cast<T> v, FmtContext &ctx) const {
		return std::formatter<T, Char_t>::format(v.value, ctx);
	}
};
