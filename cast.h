#pragma once

#include <chrono>
#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace detail::Cast {
	template <class T>
	constexpr bool is_number = std::is_arithmetic_v<T>;
	template <class Clock, class Duration>
	std::true_type is_time_point_(std::chrono::time_point<Clock, Duration>);
	std::false_type is_time_point_(...);
	template <class T>
	constexpr bool is_time_point = decltype(is_time_point_(std::declval<T>()))::value;
	template <class T>
	constexpr bool is_accepted = is_time_point<T> or is_number<T>;
} // namespace detail::Cast

template <class Internal>
	requires(detail::Cast::is_accepted<Internal>)
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
		requires(detail::Cast::is_number<To>)
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

	template <class To_Clock, class To_Duration>
		requires(detail::Cast::is_time_point<Internal>)
	operator std::chrono::time_point<To_Clock, To_Duration>() const {
		using From_Clock = Internal::clock;
		using From_Duration = Internal::duration;
		if constexpr (std::is_same_v<From_Clock, To_Clock>) {
			if constexpr (std::is_same_v<From_Duration, To_Duration>) {
				return value;
			} else {
				return std::chrono::time_point_cast<To_Duration>(value);
			}
		} else {
			return std::chrono::clock_cast<To_Clock>(value);
		}
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
		requires(std::is_arithmetic_v<T>)                                                                                                                      \
	auto operator OP(Cast<T> lhs, U rhs) {                                                                                                                     \
		using Common_Type = std::common_type_t<T, U>;                                                                                                          \
		return static_cast<Common_Type>(lhs) OP static_cast<Common_Type>(rhs);                                                                                 \
	}                                                                                                                                                          \
	template <class LHS_Clock, class LHS_Duration, class RHS_Clock, class RHS_Duration>                                                                        \
	auto operator OP(const Cast<std::chrono::time_point<LHS_Clock, LHS_Duration>> &lhs, const std::chrono::time_point<RHS_Clock, RHS_Duration> &rhs) {         \
		return static_cast<std::chrono::time_point<RHS_Clock, RHS_Duration>>(lhs) OP rhs;                                                                      \
	}                                                                                                                                                          \
	template <class LHS_Clock, class LHS_Duration, class RHS_Clock, class RHS_Duration>                                                                        \
	auto operator OP(const std::chrono::time_point<LHS_Clock, LHS_Duration> &lhs, const Cast<std::chrono::time_point<RHS_Clock, RHS_Duration>> &rhs) {         \
		return static_cast<std::chrono::time_point<RHS_Clock, RHS_Duration>>(lhs) OP rhs;                                                                      \
	}                                                                                                                                                          \
	template <class LHS_Clock, class LHS_Duration, class RHS_Clock, class RHS_Duration>                                                                        \
	auto operator OP(const Cast<std::chrono::time_point<LHS_Clock, LHS_Duration>> &lhs, const Cast<std::chrono::time_point<RHS_Clock, RHS_Duration>> &rhs) {   \
		return static_cast<std::chrono::time_point<RHS_Clock, RHS_Duration>>(lhs) OP rhs;                                                                      \
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
