#include "color.h"
#include <format>

Color Color::warning = {{.rgb = 0xBBBB44}};
Color Color::error = {{.rgb = 0xBB4444}};
Color Color::symbol = {{.rgb = 0x8888BB}};
Color Color::file = {{.rgb = 0x44BB44}};
Color Color::line = file;
Color Color::command = {{.rgb = 0x888844}};

std::string Color::operator()(std::string_view sv) {
	return std::format("{}{}{}", *this, sv, Color::reset);
}

std::ostream &operator<<(std::ostream &os, Color color) {
	return os << std::format("{}", color);
}

std::ostream &operator<<(std::ostream &os, Color::Reset) {
	return os << "\033[0m";
}
