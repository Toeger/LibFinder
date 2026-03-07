#include "color.h"
#include <format>

Color Color::warning = {{.rgb = 0xCCCC00}};
Color Color::error = {{.rgb = 0xCC0000}};
Color Color::symbol = {{.rgb = 0x8888FF}};
Color Color::file = {{.rgb = 0x00AA00}};
Color Color::command = Color::olive;

std::string Color::operator()(std::string_view sv) {
	return std::format("{}{}{}", *this, sv, Color::reset);
}

std::ostream &operator<<(std::ostream &os, Color color) {
	return os << std::format("{}", color);
}

std::ostream &operator<<(std::ostream &os, Color::Reset) {
	return os << "\033[0m";
}
