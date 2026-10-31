#pragma once

#include <string_view>

//printing type names, from https://stackoverflow.com/a/56766138

template <typename T>
consteval auto type_name() {
#define FUNCTION_NAME "type_name"
	std::string_view name, prefix, suffix;
#ifdef __clang__
	name = __PRETTY_FUNCTION__;
	prefix = "auto " FUNCTION_NAME "() [T = ";
	suffix = "]";
#elif defined(__GNUC__)
	name = __PRETTY_FUNCTION__;
	prefix = "consteval auto " FUNCTION_NAME "() [with T = ";
	suffix = "]";
#elif defined(_MSC_VER)
	name = __FUNCSIG__;
	prefix = "auto __cdecl " FUNCTION_NAME "<";
	suffix = ">(void)";
#else
#error "Unknown compiler"
#endif
#undef FUNCTION_NAME
	name.remove_prefix(prefix.size());
	name.remove_suffix(suffix.size());
	if (name == "std::basic_string<char>") {
		return std::string_view{"std::string"};
	} else if (name == "std::__cxx11::basic_string<char>") {
		return std::string_view{"std::string"};
	}
	return name;
}
