#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

class Color {
	struct Color_r_g_b_a {
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;
		std::uint8_t a = 255;
	};

	struct Color_rgba {
		std::uint32_t rgba;
	};
	struct Color_argb {
		std::uint32_t argb;
	};
	struct Color_rgb {
		std::uint32_t rgb;
	};

	public:
	constexpr Color(Color_r_g_b_a color_r_g_b_a)
		: r{color_r_g_b_a.r}
		, g{color_r_g_b_a.g}
		, b{color_r_g_b_a.b}
		, a{color_r_g_b_a.a} {}

	constexpr Color(Color_rgba color_rgba)
		: r(color_rgba.rgba >> 24 & 0xff)
		, g(color_rgba.rgba >> 16 & 0xff)
		, b(color_rgba.rgba >> 8 & 0xff)
		, a(color_rgba.rgba >> 0 & 0xff) {}

	constexpr Color(Color_argb color_argb)
		: r(color_argb.argb >> 16 & 0xff)
		, g(color_argb.argb >> 8 & 0xff)
		, b(color_argb.argb >> 0 & 0xff)
		, a(color_argb.argb >> 24 & 0xff) {}

	constexpr Color(Color_rgb color_rgb)
		: r(color_rgb.rgb >> 16 & 0xff)
		, g(color_rgb.rgb >> 8 & 0xff)
		, b(color_rgb.rgb >> 0 & 0xff)
		, a(255) {}

	constexpr Color(const Color &other) = default;
	constexpr Color &operator=(const Color &other) = default;

	std::uint8_t r{};
	std::uint8_t g{};
	std::uint8_t b{};
	std::uint8_t a{};

	constexpr std::uint32_t to_rgba() const {
		return r << 24 | g << 16 | b << 8 | a;
	}

	std::string operator()(std::string_view sv) const;

	template <class T>
	std::string operator()(const T &t) const {
		return std::format("{}{}{}", *this, t, Color::reset);
	}

	//web colors
	static const Color white, silver, gray, black, red, maroon, yellow, olive, lime, green, aqua, teal, blue, navy, fuchsia, purple;

	//usage colors
	static Color warning, error, symbol, symbol_type, file, line, command, pathdiff;

	//other
	struct Reset {
	} static constexpr reset{};
	struct Bold {
	} static constexpr bold{};
	struct Italic {
	} static constexpr italic{};
	struct Underline {
	} static constexpr underline{};

	static thread_local bool suppress;
};

std::ostream &operator<<(std::ostream &os, Color color);
std::ostream &operator<<(std::ostream &os, Color::Reset);

template <>
struct std::formatter<Color, char> {
	enum class Color_format { ansi, hex } color_format;

	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext &ctx) {
		std::string arg;
		auto it = ctx.begin();
		for (; it != ctx.end() and *it != '}'; ++it) {
			arg.push_back(*it);
		}
		if (arg == "") {
			color_format = Color_format::ansi;
		} else if (arg == "hex") {
			color_format = Color_format::hex;
		} else {
			throw std::format_error{"Invalid format specifier \"" + arg + "\" for Color"};
		}
		return it;
	}

	template <class FmtContext>
	FmtContext::iterator format(const Color &color, FmtContext &ctx) const {
		switch (color_format) {
			case Color_format::ansi:
				return Color::suppress ? ctx.out() : std::format_to(ctx.out(), "\033[38;2;{};{};{}m", color.r, color.g, color.b);
			case Color_format::hex:
				return std::format_to(ctx.out(), "{:02X}{:02X}{:02X}", color.r, color.g, color.b);
		}
		return ctx.out();
	}
};

#define X(TYPE, CODE)                                                                                                                                          \
	template <>                                                                                                                                                \
	struct std::formatter<TYPE, char> {                                                                                                                        \
		template <class ParseContext>                                                                                                                          \
		constexpr ParseContext::iterator parse(ParseContext &ctx) {                                                                                            \
			auto begin = ctx.begin();                                                                                                                          \
			if (begin != ctx.end() and *begin != '}') {                                                                                                        \
				throw std::format_error{"Invalid format specifier for " #TYPE};                                                                                \
			}                                                                                                                                                  \
			return begin;                                                                                                                                      \
		}                                                                                                                                                      \
                                                                                                                                                               \
		template <class FmtContext>                                                                                                                            \
		FmtContext::iterator format(const TYPE &, FmtContext &ctx) const {                                                                                     \
			return Color::suppress ? ctx.out() : std::format_to(ctx.out(), "\033[" CODE "m");                                                                  \
		}                                                                                                                                                      \
	};

X(Color::Reset, "")
X(Color::Bold, "1")
X(Color::Italic, "3")
X(Color::Underline, "4")
#undef X

inline constexpr Color Color::white{{.rgb = 0xFFFFFF}};
inline constexpr Color Color::silver{{.rgb = 0xC0C0C0}};
inline constexpr Color Color::gray{{.rgb = 0x808080}};
inline constexpr Color Color::black{{.rgb = 0x000000}};
inline constexpr Color Color::red{{.rgb = 0xFF0000}};
inline constexpr Color Color::maroon{{.rgb = 0x800000}};
inline constexpr Color Color::yellow{{.rgb = 0xFFFF00}};
inline constexpr Color Color::olive{{.rgb = 0x808000}};
inline constexpr Color Color::lime{{.rgb = 0x00FF00}};
inline constexpr Color Color::green{{.rgb = 0x008000}};
inline constexpr Color Color::aqua{{.rgb = 0x00FFFF}};
inline constexpr Color Color::teal{{.rgb = 0x008080}};
inline constexpr Color Color::blue{{.rgb = 0x0000FF}};
inline constexpr Color Color::navy{{.rgb = 0x000080}};
inline constexpr Color Color::fuchsia{{.rgb = 0xFF00FF}};
inline constexpr Color Color::purple{{.rgb = 0x800080}};
