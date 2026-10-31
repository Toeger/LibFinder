#pragma once

#include <algorithm>
#include <string>

struct CTS {
	constexpr const char *data() const {
		return s.data();
	}
	constexpr std::size_t size() const {
		return s.size();
	}
	std::string s;

	constexpr CTS operator+(const std::string &other) {
		return {.s = s + other};
	}

	constexpr CTS operator+(std::string_view sv) {
		return {.s = s + std::string{sv}};
	}

	constexpr CTS operator+(CTS str) {
		return {.s = s + str.s};
	}

	template <std::size_t size>
	constexpr CTS operator+(const char (&str)[size]) {
		return {.s = s + str};
	}

	constexpr CTS operator+(std::size_t n) {
		if (n == 0) {
			return {.s = "0"};
		}
		std::string result;
		while (n) {
			result += n % 10 + '0';
			n /= 10;
		}
		std::reverse(std::begin(result), std::end(result));
		return {.s = result};
	}
};
