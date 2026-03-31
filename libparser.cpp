#include "libparser.h"
#include "utility.h"

#include <algorithm>
#include <cassert>
#include <cxxabi.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <regex>
#include <set>

static std::string_view &skip_past(std::string_view &sv, char c) {
	const auto pos = sv.find(c);
	if (pos == sv.npos) {
		sv = "";
	} else {
		sv.remove_prefix(pos + 1);
	}
	return sv;
}

static void parse_lib(std::vector<Symbol> &symbol_list, std::filesystem::path path, bool include_undefined,
					  const std::vector<std::filesystem::path> &search_dirs);

std::vector<std::filesystem::path> gcc_lib_paths(std::string_view compiler) {
	const auto compiler_output = get_error_from_command(("echo | " + std::string{compiler}).c_str(), {"-E", "-v", "-"});
	constexpr auto &search_start = "\nLIBRARY_PATH=";
	std::string_view lib_paths{compiler_output};
	const auto start_pos = compiler_output.rfind(search_start);
	if (start_pos == std::string::npos) { //TODO: look for other compilers and guesses
		lib_paths = "/usr/lib/\n";
	} else {
		lib_paths.remove_prefix(start_pos + std::size(search_start) - 1);
	}

	if (auto end_pos = lib_paths.find('\n'); end_pos != std::string_view::npos) {
		lib_paths.remove_suffix(std::size(lib_paths) - end_pos);
	}

	std::vector<std::filesystem::path> result;
	std::set<std::filesystem::path> duplicates;
	for (auto lib_path : lib_paths | std::ranges::views::split(':')) {
		std::filesystem::path path{std::begin(lib_path), std::end(lib_path)};
		if (std::filesystem::exists(path)) {
			auto canonical_path = std::filesystem::canonical(path);
			if (duplicates.insert(canonical_path).second) {
				result.push_back(std::move(canonical_path));
			}
		}
	}
	return result;
}

void for_each_path(std::string_view file, const std::filesystem::path &path1, const std::vector<std::filesystem::path> &paths, auto &&function) {
	auto p = path1;
	auto candidate = p.remove_filename() / file;
	if (std::filesystem::exists(candidate)) {
		function(candidate);
		return;
	}
	for (auto &path : paths) {
		candidate = path / file;
		if (std::filesystem::exists(candidate)) {
			function(candidate);
			return;
		}
	}
	std::string dirs = path1;
	for (auto &path : paths) {
		dirs += '\n' + std::string{path};
	}
	std::println(stderr, "Failed finding file {} required by {} in any of the include directories:\n{}", file, path1.string(), dirs);
}

static void parse_linker_script(std::vector<Symbol> &symbol_list, std::filesystem::path path, bool include_undefined,
								const std::vector<std::filesystem::path> &search_dirs) {
	std::ifstream file{path};
	std::string content(std::istreambuf_iterator<char>{file}, {});
	static const std::regex regex{R"(^(?:(?:\/\*[^*]*(?!\/)\*\/)|([A-Z_]+)[ \t]*\((.*)\)[ \t]*)\n)", std::regex::optimize};
	std::smatch match;
	std::string::difference_type offset = 0;
	while (std::regex_search(std::cbegin(content) + offset, std::cend(content), match, regex)) {
		assert(match.ready());
		offset += match[0].length();
		if (not match[1].length()) {
			continue;
		}
		auto &command = match[1];
		auto &value = match[2];
		if (command == "INCLUDE") {
			std::string_view include_list{value.first, value.second};
			while (include_list.starts_with(' ')) {
				include_list.remove_prefix(1);
			}
			while (not include_list.empty()) {
				std::string_view include_file{std::begin(include_list), include_list.find(' ')};
				include_list.remove_prefix(include_file.size() + 1);
				if (include_file.starts_with('/')) {
					parse_linker_script(symbol_list, include_file, include_undefined, search_dirs);
				} else {
					auto parse_script = [&include_undefined, &search_dirs, &symbol_list](const std::filesystem::path &include_path) {
						parse_linker_script(symbol_list, include_path, include_undefined, search_dirs);
					};
					for_each_path(include_file, path, search_dirs, parse_script);
				}
			}
		} else if (command == "INPUT" or command == "GROUP") {
			std::string_view lib_list{value.first, value.second};
			while (not lib_list.empty()) {
				std::string_view lib_file{std::begin(lib_list), std::find_if(std::begin(lib_list), std::end(lib_list),
																			 [](char c) { return c == ' ' or c == '(' or c == ')' or c == '\n'; })};
				if (lib_file.empty()) {
					lib_list.remove_prefix(1);
					continue;
				}
				lib_list.remove_prefix(lib_file.size());
				if (lib_file.starts_with('/')) {
					parse_lib(symbol_list, lib_file, include_undefined, search_dirs);
					continue;
				}
				if (lib_file == "AS_NEEDED") {
					continue;
				}
				auto parse_lib = [&include_undefined, &search_dirs, &symbol_list](const std::filesystem::path &include_path) {
					::parse_lib(symbol_list, include_path, include_undefined, search_dirs);
				};
				if (lib_file.starts_with("-l")) {
					lib_file.remove_prefix(2);
					std::string lib_candidate{"lib"};
					lib_candidate += lib_file;
					lib_candidate += ".a";
					for_each_path(lib_candidate, path, search_dirs, parse_lib);
				} else { //assume it's a file name
					for_each_path(lib_file, path, search_dirs, parse_lib);
				}
			}
		} else if (command == "SEARCH_DIR") {
			assert(!"TODO");
		} else if (command == "STARTUP") {
			assert(!"TODO");
		}

		//std::cout << "Command: " << match[1] << '\n';
		//std::cout << "Value: " << match[2] << '\n';
	}
	if (std::begin(content) + offset != std::end(content)) {
		std::string_view line{std::begin(content) + offset, std::find(std::begin(content) + offset, std::end(content), '\n')};
		throw std::runtime_error{std::format("Failed parsing linker script {} in line\n{}", path.c_str(), line)};
	}
	(void)include_undefined;
	(void)symbol_list;
}

static void parse_lib(std::vector<Symbol> &symbol_list, std::filesystem::path path, bool include_undefined,
					  const std::vector<std::filesystem::path> &search_dirs) {
	auto parse_fail = [](std::string_view message) {
		std::cerr << message << '\n';
		exit(-1);
	};
	auto symbols = get_output_from_command("objdump", {"-tTw", std::string{path}});
	if (symbols.empty()) {
		parse_linker_script(symbol_list, path, include_undefined, search_dirs);
		return;
	}
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
				continue;
			case '!': //local and global
				break;
			case ' ': //undefined
				if (not include_undefined) {
					continue;
				}
				break;
		}
		Symbol_type type;
		if (line.contains(" .hidden ")) {
			continue;
		}
		if (line.starts_with("*UND*\t")) {
			if (not include_undefined) {
				continue;
			}
			type = Symbol_type::undefined;
		} else {
			type = flags[1] == 'w' ? Symbol_type::defined_weak : Symbol_type::defined;
		}
		if (auto pos = line.rfind(' '); pos != line.npos) {
			line.remove_prefix(pos + 1);
			if (line.contains('.')) {
				continue;
			}
			symbol_list.push_back(Symbol{.type = type, .mangled_name = std::string{line}});
		} else {
			parse_fail(std::format("Failed parsing line for {}:\n{}\nSpace expected", path.c_str(), line));
			continue;
		}
	}
}

std::vector<Symbol> parse_lib(std::filesystem::path path, bool include_undefined, const std::vector<std::filesystem::path> &search_dirs) {
	std::vector<Symbol> symbol_list;
	parse_lib(symbol_list, path, include_undefined, search_dirs);
	return symbol_list;
}

std::string Symbol::demangled_name() const {
	return demangled_name(mangled_name);
}

std::string Symbol::base_name() const {
	auto name = demangled_name();
	std::string_view trunc = name;
	narrow_to_base_name(trunc);
	return std::string{trunc};
}

void Symbol::narrow_to_base_name(std::string_view &name) {
	for (std::size_t i = 0; i < std::size(name); i++) {
		switch (name[i]) {
			case '(':
			case ' ':
			case '[':
			case '<':
				assert(i != 0);
				name.remove_suffix(std::size(name) - i);
		}
	}
}

std::string Symbol::demangled_name(std::string name) {
	int status = 1;
	std::unique_ptr<char, decltype([](char *s) { std::free(s); })> p{abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status)};
	if (status) {
		const char *error;
		switch (status) {
			case -1:
				error = "memory allocation failed";
				break;
			case -2:
				error = "invalid mangled name";
				break;
			case -3:
				error = "invalid argument";
				break;
			default:
				error = "unknown error";
				break;
		}
		throw std::runtime_error{std::format("Failed demangling '{}' because {}", name, error)};
	}
	return p.get();
}
