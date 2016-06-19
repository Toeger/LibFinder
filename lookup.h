#ifndef LOOKUP_H
#define LOOKUP_H

#include "main.h"

#include <string>
#include <vector>

struct Symbol_lib_entry {
	Symbol_lib_entry(std::string &&entry)
		: entry(entry) {
		string_view rest(entry);
		rest.remove_prefix(entry.find(file_separator) + 1);
		for (auto pos = rest.find(file_separator); pos != rest.npos; pos = rest.find(file_separator)) {
			lib_pos.emplace_back(rest.data() - entry.data(), pos);
			rest.remove_prefix(pos + 1);
		}
		lib_pos.emplace_back(rest.data() - entry.data(), rest.size());
	}
	std::string entry;
	string_view get_symbol() {
		return string_view(entry.data(), lib_pos.front().first - 1);
	}
	std::vector<std::pair<int, int>> lib_pos;
	std::vector<string_view> get_libs_view() const {
		std::vector<string_view> retval;
		retval.reserve(lib_pos.size());
		for (auto &pos : lib_pos) {
			retval.emplace_back(entry.data() + pos.first, pos.second);
		}
		return retval;
	}
};

std::vector<std::string> exact_lookup(string_view symbol);
std::vector<Symbol_lib_entry> prefix_lookup(string_view symbol);

#endif // LOOKUP_H