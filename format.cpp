#include "format.h"
#include "color.h"

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

std::string diff_highlight(std::string_view text_before, std::string text, std::string_view text_after) {
	constexpr auto max_diff = 10l;
	struct Diff {
		operator bool() const {
			return start < end and end - start < max_diff;
		}

		bool overlaps(const Diff &other) const {
			return start > other.end or other.start > end;
		}

		Diff combined_with(const Diff &other) const {
			return {std::min(start, other.start), std::max(end, other.end)};
		}

		long start{};
		long end{};
	};

	auto get_diffs = [](std::string_view original, std::string_view comp) -> Diff {
		auto diff_end_its = std::mismatch(std::rbegin(comp), std::rend(comp), std::rbegin(original), std::rend(original));
		auto diff_ends = std::pair{std::rend(comp) - diff_end_its.first, std::rend(original) - diff_end_its.second};
		if (diff_ends.second == 0) {
			return {};
		}
		auto diff_start_its = std::mismatch(std::begin(comp), std::end(comp), std::begin(original), std::end(original));
		auto diff_starts = std::pair{diff_start_its.first - std::begin(comp), diff_start_its.second - std::begin(original)};
		if (not Diff{diff_starts.first, diff_ends.first}) {
			return {};
		}
		return {diff_starts.second, diff_ends.second};
	};
	if (text == "/usr/lib/x86_64-linux-gnu/libc.so") {
		text += "";
	}
	auto diff1 = get_diffs(text, text_before);
	auto diff2 = get_diffs(text, text_after);
	if (not diff1 and not diff2) {
		return text;
	}
	if (diff1 and diff2) {
		if (diff1.overlaps(diff2)) {
			diff1 = diff1.combined_with(diff2);
			diff1 = {};
		} else if (diff1.start > diff2.start) {
			std::swap(diff1, diff2);
		}
	}
	auto highlight = [](std::string &text_, Diff diff) {
		if (not diff) {
			return;
		}
		text_ = std::format("{}{}{}{}{}", std::string_view{std::begin(text_), std::begin(text_) + diff.start}, Color::bold,
							std::string_view{std::begin(text_) + diff.start, std::begin(text_) + diff.end}, Color::reset,
							std::string_view{std::begin(text_) + diff.end, std::end(text_)});
	};
	highlight(text, diff2);
	highlight(text, diff1);

	return text;
}
