#include "symbol_matcher.h"
#include "external/nlohmann/json/json.hpp"
#include "utility.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>

Symbol_matcher::Symbol_matcher(std::filesystem::path data_base)
	: db{data_base} {}

void Symbol_matcher::add(Symbol symbol, std::filesystem::path origin) {
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
					throw Duplicate_symbol_error{{.symbol = symbol, .lib1 = origin, .lib2 = origins[it->second.origin_index].c_str()}};
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
	compile_commands_json_path = file_path;
	compile_commands_json_path.remove_filename();
	std::ifstream compile_commands{file_path};
	if (not compile_commands) {
		throw std::runtime_error{std::format("Failed opening file {}", file_path.c_str())};
	}
	for (auto &obj : nlohmann::json::parse(compile_commands)) {
		std::filesystem::path directory{obj["directory"]};
		std::filesystem::path file{obj["file"]};
		std::filesystem::path output{obj["output"]};
		compiled_to_source_file[directory / output] = file;
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
		path_priorities[output_path] = gcc_lib_paths();
	}
}

void Symbol_matcher::add_lib(std::filesystem::path lib, const std::vector<std::filesystem::path> &lib_paths) {
	for (auto &symbol : parse_lib(lib, true, lib_paths)) {
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

std::size_t Symbol_matcher::origin_index(std::filesystem::path origin) {
	auto pos = std::ranges::find(origins, origin);
	std::size_t index = pos - std::begin(origins);
	if (index == std::size(origins)) {
		origins.emplace_back(origin);
	}
	return index;
}

std::string Symbol_matcher::resolve_to_command() {
	/* TODO
	 * Implement proper rules to resolve library candidates
	 * Deduplicate based on content
	 */
	for (auto it = std::begin(undefined); it != std::end(undefined);) {
		auto &[symbol, type_origin] = *it;
		auto libs = db.libraries_from_symbol(symbol);
		//libs.erase(std::remove_if(std::begin(libs), std::end(libs), [](std::string_view sv) { return sv.starts_with("/snap"); }), std::end(libs));
		{
			std::multimap<std::ptrdiff_t /*priority*/, std::filesystem::path /*library*/> priorities;
			for (const auto &lib : libs) {
				auto lib_path = lib;
				lib_path.remove_filename();
				if (auto path_priorities_it = path_priorities.find(origins[type_origin.origin_index]); path_priorities_it != std::end(path_priorities)) {
					auto &priority_paths = path_priorities_it->second;
					std::ptrdiff_t priority = std::ranges::find(priority_paths, lib_path) - std::begin(priority_paths);
					priorities.insert({priority, lib});
				} else {
					std::println(stderr, "Warning: {} was not mentioned in the compile_commands.json", origins[type_origin.origin_index].c_str());
					std::println(stderr, "List of files:");
					for (auto &[f, _] : path_priorities) {
						std::println(stderr, "{}", f.c_str());
					}
				}
			}

			if (not priorities.empty()) {
				auto lowest_priority = std::begin(priorities)->first;
				auto range = priorities.equal_range(lowest_priority);
				libs.assign_range(std::ranges::subrange(range.first, range.second) | std::views::values);
			}
		}
		if (libs.empty()) {
			//std::cout << symbol << " / " << Symbol{.type = Symbol_type::undefined, .name = symbol}.demangled_name() << '\n';
			//++it;
			//continue;
			throw Unresolved_result{{
				.symbol = {.type = Symbol_type::undefined, .name = symbol},
				.compiled_file = origins[type_origin.origin_index],
				.source_file = compiled_to_source_file[origins[type_origin.origin_index]],
				.compile_commands_json_path = compile_commands_json_path,
			}};
		}
		if (libs.size() == 1) {
			add_lib(std::string{libs[0]}, gcc_lib_paths()); //invalidates current iterator
			it = std::begin(undefined);
			continue;
		}

#if 0
		//auto &origin = origins[type_origin.origin_index];
		for (auto &file : files) {
			if (auto path_it = path_priorities.find(file); path_it != std::end(path_priorities)) {
				auto &priorities = path_it->second;
				auto min_it =
					std::min_element(std::begin(files), std::end(files), [&priorities](const std::filesystem::path &lhs, const std::filesystem::path &rhs) {
						return std::find(std::begin(priorities), std::end(priorities), lhs) - std::begin(priorities) <
							   std::find(std::begin(priorities), std::end(priorities), rhs) - std::begin(priorities);
					});
				if (auto pf_it = std::ranges::find(priorities, file); pf_it != std::end(priorities)) {
				}
			}
		}
#endif
		++it; //ignore ambiguous options for now
	}

	std::vector<std::vector<std::filesystem::path>> lib_sets;
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
	//std::cout << "Lib combos:\n";
	//for (auto &lib_set : lib_sets) {
	//	std::cout << "[\n\t";
	//	for (char c : std::ranges::join_with_view(lib_set, std::string_view{"\n\t"})) {
	//		std::cout << c;
	//	}
	//	std::cout << "\n]\n";
	//}

	std::string result;
	return result;
}

bool Symbol_matcher::is_resolved() const {
	return undefined.empty();
}

static std::string unresolved_result_error_string(const Symbol_matcher::Unresolved_result::Unresolved_result_aggregate &unresolved) {
	const auto &demangled_name = unresolved.symbol.demangled_name();
	const auto &locations = get_locations(demangled_name, unresolved.source_file, unresolved.compile_commands_json_path);
	std::string locations_string;
	if (locations.size() == 1) {
		locations_string = std::format("{}:{}", locations[0].first.string(), locations[0].second);
	} else {
		locations_string = "one of";
		for (auto &location : locations) {
			locations_string += std::format("\n{}:{}", location.first.string(), location.second);
		}
	}
	return std::format("Error: Unresolved symbol {} / {} required by {} declared in {}", demangled_name, unresolved.symbol.name,
					   unresolved.compiled_file.c_str(), locations_string);
}

Symbol_matcher::Unresolved_result::Unresolved_result(Unresolved_result_aggregate &&unresolved)
	: std::runtime_error{unresolved_result_error_string(unresolved)}
	, symbol{std::move(unresolved.symbol)}
	, source_file{std::move(unresolved.source_file)}
	, compiled_file{std::move(unresolved.compiled_file)} {}

Symbol_matcher::Duplicate_symbol_error::Duplicate_symbol_error(Duplicate_symbol_error_aggregate dsea)
	: std::runtime_error{std::format("Error: Duplicate symbol definition for {} in\n{} and\n{}", dsea.symbol.demangled_name(), dsea.lib1.c_str(),
									 dsea.lib2.c_str())}
	, symbol{std::move(dsea.symbol)}
	, lib1{std::move(dsea.lib1)}
	, lib2{std::move(dsea.lib2)} {}
