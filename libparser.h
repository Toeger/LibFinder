#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class Symbol_type : char {
	undefined = 'U',
	defined = 'T',
	defined_weak = 'W',
};

struct Symbol {
	struct Aggregate {
		Symbol_type type = Symbol_type::undefined;
		std::string mangled_name;
	};
	Symbol(Aggregate &&aggregate);
	Symbol(Symbol_type type, std::string mangled_name);
	std::string demangled_name() const;
	std::string base_name() const;

	Symbol_type type;
	std::string mangled_name;

	static void narrow_to_base_name(std::string_view &name);
	static std::string demangled_name(std::string mangled_name);

	friend std::ostream &operator<<(std::ostream &os, const Symbol &symbol);
};

std::vector<Symbol> parse_lib(std::filesystem::path path, bool include_undefined, const std::vector<std::filesystem::path> &search_dirs);

std::vector<std::filesystem::path> gcc_lib_paths(std::string_view compiler = "gcc");
