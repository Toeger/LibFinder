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
	Symbol_type type;
	std::string name;
	std::string demangled_name() const;
	static std::string demangled_name(std::string name);
};

std::vector<Symbol> parse_lib(std::filesystem::path path, bool include_undefined, const std::vector<std::filesystem::path> &search_dirs);

std::vector<std::filesystem::path> gcc_lib_paths(std::string_view compiler = "gcc");
