#include "libparser.h"
#include "utility.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <ranges>

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
		Symbol_type type;
		if (line.contains(" *UND*\t")) {
			type = Symbol_type::undefined;
		} else if (line.contains(" .text\t") or line.contains(" .data") or line.contains(" .bss\t") or line.contains(" .rodata\t") or
				   line.contains(" .tdata\t") or line.contains(" .fini_array\t") or line.contains(" .tbss\t")) {
			type = Symbol_type::defined;
			//} else if (line.contains(" .init\t") or line.contains(" *ABS*\t") or line.contains(" .fini\t") or
			//		   line.contains(" .got.plt\t")) { //TODO: Find out what these mean
			//	continue;
		} else {
			//parse_fail(std::format("Failed parsing line for {}:\n{}\n*UND* or .text or .init expected", path, line));
			continue;
		}
		if (auto pos = line.rfind(' '); pos != line.npos) {
			line.remove_prefix(pos + 1);
			result.push_back(Symbol{.type = type, .name = std::string{line}});
		} else {
			parse_fail(std::format("Failed parsing line for {}:\n{}\nSpace expected", path.string(), line));
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
