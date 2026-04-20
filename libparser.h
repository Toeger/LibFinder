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
	struct Symbol_Aggregate {
		Symbol_type type;
		std::string mangled_name;
	};
	Symbol(Symbol_Aggregate &&aggregate);
	Symbol(Symbol_type type, std::string mangled_name);
	Symbol_type type;
	std::string mangled_name;
	std::string demangled_name() const;
	std::string base_name() const;
	static void narrow_to_base_name(std::string_view &name);
	static std::string demangled_name(std::string mangled_name);
	static void canonicalize_mangling(std::string &mangled_name);
};

std::vector<Symbol> parse_lib(std::filesystem::path path, bool include_undefined, const std::vector<std::filesystem::path> &search_dirs);

std::vector<std::filesystem::path> gcc_lib_paths(std::string_view compiler = "gcc");
