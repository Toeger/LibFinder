#include "format.h"

#include <format>

std::string bytes(std::size_t byte_count) {
	constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB", "RB", "QB"};
	const char *const *unit = units;
	double display_bytes = byte_count;
	while (display_bytes >= 10000) {
		display_bytes /= 1024;
		unit++;
	}
	return unit == units ? std::format("{}{}", display_bytes, *unit) : std::format("{:.1f}{}", display_bytes, *unit);
};

std::string number(std::uint64_t number) {
	char buffer[26];
	char *pos = buffer + sizeof buffer;
	while (number >= 1000) {
		pos -= 4;
		std::format_to(pos, "'{:03}", number % 1000);
		number /= 1000;
	}
	while (number >= 10) {
		*--pos = number % 10 + '0';
		number /= 10;
	}
	*--pos = number + '0';
	return {pos, buffer + sizeof buffer};
};
