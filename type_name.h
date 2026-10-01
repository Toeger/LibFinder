#pragma once

#include <string_view>

//printing type names, from https://stackoverflow.com/a/56766138

template <typename T>
consteval auto type_name() {
#define PREFIX
	std::string_view name, prefix, suffix;
#ifdef __clang__
	name = __PRETTY_FUNCTION__;
	prefix = "auto " PREFIX "type_name() [T = ";
	suffix = "]";
#elif defined(__GNUC__)
	name = __PRETTY_FUNCTION__;
	prefix = "consteval auto " PREFIX "type_name() [with T = ";
	suffix = "]";
#elif defined(_MSC_VER)
	name = __FUNCSIG__;
	prefix = "auto __cdecl " PREFIX "type_name<";
	suffix = ">(void)";
#endif
#undef PREFIX
	name.remove_prefix(prefix.size());
	name.remove_suffix(suffix.size());
	if (name == "std::basic_string<char>") {
		return std::string_view{"std::string"};
	} else if (name == "std::__cxx11::basic_string<char>") {
		return std::string_view{"std::string"};
	}
	return name;
}
