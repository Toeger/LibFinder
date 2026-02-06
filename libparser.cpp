#include "libparser.h"
#include "utility.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <ranges>

static std::string_view &skip_past(std::string_view &sv, char c) {
	const auto pos = sv.find(c);
	if (pos == sv.npos) {
		sv = "";
	} else {
		sv.remove_prefix(pos + 1);
	}
	return sv;
}

std::vector<Symbol> parse_lib(std::filesystem::path path) {
	auto parse_fail = [](std::string_view message) {
		std::cerr << message << '\n';
		exit(-1);
	};
	std::vector<Symbol> result;
	auto symbols = get_output_from_command("objdump", {"-tTw", std::string{path}});
	for (auto line_it : std::ranges::split_view{std::string_view{symbols}, std::string_view{"\n"}}) {
		std::string_view line{line_it};
		if (line.size() < 40) {
			continue;
		}
		if (line.front() < '0' or line.front() > '9') {
			continue;
		}
		std::string_view flags = {skip_past(line, ' ').begin(), 7};
		line.remove_prefix(8);
		switch (flags[0]) {
			case 'l': //local
				continue;
			case 'g': //global
				break;
			case 'u': //unique global
				break;
			case '!': //local and global
				break;
			case ' ': //undefined
				break;
		}
		Symbol_type type;
		if (line.contains(" .hidden ")) {
			continue;
		}
		if (line.starts_with("*UND*\t")) {
			type = Symbol_type::undefined;
		} else {
			type = flags[1] == 'w' ? Symbol_type::defined_weak : Symbol_type::defined;
		}
		if (auto pos = line.rfind(' '); pos != line.npos) {
			line.remove_prefix(pos + 1);
			if (line.contains('.')) {
				continue;
			}
			result.push_back(Symbol{.type = type, .name = std::string{line}});
		} else {
			parse_fail(std::format("Failed parsing line for {}:\n{}\nSpace expected", path.c_str(), line));
			continue;
		}
	}
	return result;
}

std::string Symbol::demangled_name() const {
	return demangled_name(name);
}

std::string Symbol::demangled_name(std::string name) {
	auto result = get_output_from_command("c++filt", {std::move(name)});
	result.pop_back();
	return result;
}
