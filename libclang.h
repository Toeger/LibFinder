#pragma once

#include "color.h"

#include <filesystem>
#include <map>
#include <memory>
#include <ostream>

struct Libclang {
	struct Libclang_Pimpl;

	struct Location {
		std::filesystem::path path{};
		unsigned int line{};

		auto operator<=>(Location const &) const = default;
		operator bool() const {
			return line;
		}

		friend std::ostream &operator<<(std::ostream &os, const Location &location) {
			if (location.line) {
				return os << Color::file(location.path.string()) << ':' << Color::line(location.line);
			}
			return os << Color::file("<unknown location>");
		}
	};

	struct Symbol_Location {
		Location declaration{};
		bool is_definition{};
		Location usage{};
		friend std::ostream &operator<<(std::ostream &os, const Symbol_Location &location) {
			os << (location.is_definition ? "Definition" : "Declaration") << " at " << location.declaration << '\n';
			os << "Usage at " << location.usage << '\n';
			return os;
		}
	};

	Libclang(std::filesystem::path path, std::filesystem::path compile_commands_json_directory);
	Libclang(Libclang &&);
	Libclang &operator=(Libclang &&);
	~Libclang();

	Symbol_Location get_locations(std::string_view mangled_name);
	void get_locations(std::map<std::string_view /*mangled_name*/, Symbol_Location> &name_locations);

	std::filesystem::path path;
	std::unique_ptr<Libclang_Pimpl> pimpl;
};

template <>
struct std::formatter<Libclang::Location, char> {
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext &ctx) {
		auto it = ctx.begin();
		if (it != ctx.end() and *it != '}') {
			throw std::format_error{"Invalid format specifier for Libclang::Location"};
		}
		return it;
	}

	template <class FmtContext>
	FmtContext::iterator format(const Libclang::Location &location, FmtContext &ctx) const {
		if (location.line) {
			return std::format_to(ctx.out(), "{}{}{}:{}{}{}", Color::file, location.path.string(), Color::reset, Color::file, location.line, Color::reset);
		}
		return std::format_to(ctx.out(), "<invalid location>");
	}
};
