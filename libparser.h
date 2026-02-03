#pragma once

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

std::vector<Symbol> parse_lib(std::string_view path);
