#include "symbol_matcher.h"
#include "external/nlohmann/json/json.hpp"
#include "utility.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>

Symbol_matcher::Symbol_matcher(std::string_view data_base)
	: db{data_base} {}

void Symbol_matcher::add(Symbol symbol, std::string_view origin) {
	switch (symbol.type) {
		case Symbol_type::undefined:
			if (defined.contains(symbol.name)) {
				return;
			}
			undefined[symbol.name] = {symbol.type, origin_index(origin)};
			break;
		case Symbol_type::defined:
			if (undefined.contains(symbol.name)) {
				undefined.erase(symbol.name);
			}
			if (auto it = defined.find(symbol.name); it != std::end(defined)) {
				if (it->second.type != Symbol_type::defined_weak) {
					throw std::runtime_error{std::format("Error: Duplicate symbol definition for {}\nDefined in {} and {}", symbol.demangled_name(), origin,
														 origins[it->second.origin_index])};
				}
			} else {
				defined[symbol.name] = {symbol.type, origin_index(origin)};
			}
			break;
		case Symbol_type::defined_weak:
			if (undefined.contains(symbol.name)) {
				undefined.erase(symbol.name);
			}
			defined.insert({symbol.name, {symbol.type, origin_index(origin)}});
			break;
	}
}

void Symbol_matcher::load_compile_commands_json(std::filesystem::path file_path) {
	std::ifstream compile_commands{file_path};
	if (not compile_commands) {
		throw std::runtime_error{std::format("Failed opening file {}", file_path.string())};
	}
	for (auto &obj : nlohmann::json::parse(compile_commands)) {
		std::filesystem::path directory{obj["directory"]};
		std::filesystem::path file{obj["file"]};
		std::filesystem::path output{obj["output"]};
		std::vector<std::string> args;
		if (auto it = obj.find("args"); it != std::end(obj)) {
			for (auto &arg : *it) {
				args.push_back(arg);
			}
		} else {
			std::string arg;
			bool escape = false;
			bool quoted = false;
			for (char c : std::string{obj["command"]}) {
				switch (c) {
					case ' ':
						if (quoted) {
							arg.push_back(c);
						} else {
							if (not arg.empty()) {
								args.push_back(std::move(arg));
								arg.clear();
							}
						}
						break;
					case '\\':
						if (escape) {
							arg.push_back(c);
						} else {
							escape = true;
						}
						break;
					case '"':
						if (escape) {
							arg.push_back(c);
							break;
						}
						quoted = not quoted;
						break;
					default:
						if (escape) {
							throw std::runtime_error{std::format("Unexpected escape sequence \\{}", c)};
						}
						arg.push_back(c);
						break;
				}
			}
			if (escape) {
				throw std::runtime_error{"Parse error: unexpected \\"};
			}
			if (quoted) {
				throw std::runtime_error{"Parse error: unescaped quote"};
			}
			if (not arg.empty()) {
				args.push_back(std::move(arg));
			}
		}
		if (args.empty()) {
			throw std::runtime_error{"Empty command"};
		}
		std::filesystem::path output_path{directory / output};
		auto &priorities = path_priorities[output_path];
		auto &compiler = args[0];
		auto compiler_output = get_output_from_command(compiler.c_str(), {"-E", "-v", "-"});
		//std::println(stderr, "Output: {}", compiler_output);

		//views | std::ranges::views::filter([](std::string_view line) { return line.starts_with("LIBRARY_PATH="); });
		for (auto lib : compiler_output | std::ranges::views::split('\n') | std::ranges::views::filter([](auto line) {
							return std::string_view{line}.starts_with("LIBRARY_PATH=");
						}) | std::ranges::views::take(1) //| std::ranges::views::split(':')
		) {
			std::cerr << std::string_view{lib} << '\n';
		}
		priorities.push_back({});
	}
}

void Symbol_matcher::add_lib(std::string lib) {
	auto file_type = get_output_from_command("file", {lib});
	if (file_type.contains("symbolic link")) {
		auto target = get_output_from_command("readlink", {"-f", lib});
		target.pop_back();
		file_type = get_output_from_command("file", {target});
	}
	bool is_shared_object = file_type.contains("ELF 64-bit LSB shared object");
	if (not is_shared_object and not file_type.contains("current ar archive")) {
		throw std::runtime_error{std::format("{} is not accessible", lib)};
	}

	for (auto &symbol : parse_lib(lib)) {
		switch (symbol.type) {
			case Symbol_type::defined:
				undefined.erase(symbol.name);
				if (auto it = defined.find(symbol.name); it != std::end(defined)) {
					if (it->second.type != Symbol_type::defined_weak) {
						throw Duplicate_symbol_error{{.symbol = symbol, .lib1 = lib, .lib2 = origins[it->second.origin_index]}};
					}
					it->second.type = symbol.type;
				} else {
					defined.insert({symbol.name, {.type = symbol.type, .origin_index = origin_index(lib)}});
				}
				break;
			case Symbol_type::defined_weak:
				undefined.erase(symbol.name);
				defined.insert({symbol.name, {.type = symbol.type, .origin_index = origin_index(lib)}});
				break;
			case Symbol_type::undefined:
				if (defined.contains(symbol.name)) {
					break;
				}
				undefined.insert({symbol.name, {.type = symbol.type, .origin_index = origin_index(lib)}});
				break;
		}
	}
}

std::size_t Symbol_matcher::origin_index(std::string_view origin) {
	auto pos = std::ranges::find(origins, origin);
	std::size_t index = pos - std::begin(origins);
	if (index == std::size(origins)) {
		origins.emplace_back(origin);
	}
	return index;
}

std::string Symbol_matcher::resolve_to_command() {
	std::vector<std::string> libraries;
	for (auto it = std::begin(undefined); it != std::end(undefined);) {
		auto &[symbol, type_origin] = *it;
		auto libs = db.libraries_from_symbol(symbol);
		libs.erase(std::remove_if(std::begin(libs), std::end(libs), [](std::string_view sv) { return sv.starts_with("/snap"); }), std::end(libs));
		if (libs.empty()) {
			throw Unresolved_result{{.symbol = symbol, .origin = origins[type_origin.origin_index]}};
		}
		if (libs.size() == 1) {
			add_lib(std::string{libs[0]}); //invalidates current iterator
			it = std::begin(undefined);
			continue;
		}
		++it; //ignore ambiguous options for now
	}

	std::vector<std::vector<std::string_view>> lib_sets;
	for (auto &[symbol, type_origin] : undefined) {
		if (type_origin.type == Symbol_type::undefined) {
			auto libs = db.libraries_from_symbol(symbol);
			//libs.erase(std::remove_if(std::begin(libs), std::end(libs), [](std::string_view sv) { return sv.starts_with("/snap"); }), std::end(libs));
			std::ranges::sort(libs);
			libs.erase(std::unique(std::begin(libs), std::end(libs)), std::end(libs));
			lib_sets.push_back(std::move(libs));
		}
	}
	assert(std::ranges::includes(lib_sets[1], lib_sets[2]));
	for (std::size_t i = 0; i + 1 < std::size(lib_sets);) {
		for (std::size_t j = i + 1; j < std::size(lib_sets);) {
			if (std::ranges::includes(lib_sets[j], lib_sets[i])) {
				lib_sets.erase(std::begin(lib_sets) + j);
			} else if (std::ranges::includes(lib_sets[i], lib_sets[j])) {
				lib_sets.erase(std::begin(lib_sets) + i);
				j = i + 1;
			} else {
				j++;
			}
		}
		i++;
	}
	std::cout << "Lib combos:\n";
	for (auto &lib_set : lib_sets) {
		std::cout << '[';
		for (char c : std::ranges::join_with_view(lib_set, std::string_view{", "})) {
			std::cout << c;
		}
		std::cout << "]\n";
	}

	std::string result;
	return result;
}
bool Symbol_matcher::is_resolved() const {
	return undefined.empty();
}
