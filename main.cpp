#include "main.h"
#include "generate.h"
#include "libparser.h"
#include "lookup.h"
#include "test.h"
#include "utility.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <expected>
#include <format>
#include <iostream>
#include <print>
#include <thread>

const std::string data_base_path = [] {
	auto username = get_output_from_command("whoami");
	username.pop_back(); //remove newline
	return "/home/" + username + "/.libfinder";
}();

//TODO: find a way to share the files between users
const std::string data_base_filepath = data_base_path + "/database";
const std::string tree_filepath = data_base_path + "/libs";
const std::string data_base_index_filepath = data_base_path + "/database_index";

struct Symbol_matcher {
	struct Type_and_origin_index {
		Symbol_type type;
		std::size_t origin_index;
	};

	void add(Symbol symbol, std::string_view origin) {
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

	struct Unresolved_result {
		std::string symbol;
		std::string origin;
	};

	[[nodiscard]] std::expected<std::string, Unresolved_result> resolve_to_command() {
		std::vector<std::string> libraries;
		for (auto it = std::begin(undefined); it != std::end(undefined);) {
			auto libs = exact_lookup(it->first);
			if (libs.empty()) {
				return std::unexpected(Unresolved_result{it->first, origins[it->second.origin_index]});
			}
			if (libs.size() == 1) {
				add_lib(libs[0]); //invalidates current iterator
				it = std::begin(undefined);
				continue;
			}
			++it; //ignore ambiguous options for now
		}
		std::string result;
		const char *sep = "";
		for (auto &lib : libraries) {
			result += sep + lib;
			sep = " ";
		}
		return result;
	}

	struct Duplicate_symbol_error {
		Symbol symbol;
		std::string lib1, lib2;
	};

	std::expected<void, Duplicate_symbol_error> add_lib(std::string_view lib) {
		auto file_type = get_output_from_command(std::format("file \"{}\"", lib));
		if (file_type.contains("symbolic link")) {
			auto target = get_output_from_command(std::format("readlink -f \"{}\"", lib));
			target.pop_back();
			file_type = get_output_from_command("file \"" + target + '"');
		}
		bool is_shared_object = file_type.contains("ELF 64-bit LSB shared object");
		if (not is_shared_object and not file_type.contains("current ar archive")) {
			throw std::runtime_error{std::format("{} is not accessible", lib)};
		}

		for (auto &symbol : parse_lib(lib, is_shared_object)) {
			switch (symbol.type) {
				case Symbol_type::defined:
					undefined.erase(symbol.name);
					if (auto it = defined.find(symbol.name); it != std::end(defined)) {
						if (it->second.type != Symbol_type::defined_weak) {
							return std::unexpected{
								Duplicate_symbol_error{.symbol = symbol, .lib1 = std::string{lib}, .lib2 = origins[it->second.origin_index]}};
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
		return {};
	}

	bool is_resolved() const {
		return undefined.empty();
	}

	std::size_t origin_index(std::string_view origin) {
		auto pos = std::ranges::find(origins, origin);
		std::size_t index = pos - std::begin(origins);
		if (index == std::size(origins)) {
			origins.emplace_back(origin);
		}
		return index;
	}

	std::map<std::string /*symbol*/, Type_and_origin_index> defined, undefined;
	std::vector<std::string /*paths*/> origins;
};

static void handle_linkcommand(std::span<std::string_view> files) {
	Symbol_matcher symbol_matcher;
	for (auto &file : files) {
		for (auto &symbol : parse_lib(file, false)) {
			if (symbol.name != "_GLOBAL_OFFSET_TABLE_") {
				symbol_matcher.add(symbol, file);
			}
		}
	}
	std::println("{} defined and {} undefined symbols found before linking", symbol_matcher.defined.size(), symbol_matcher.undefined.size());
	auto result = symbol_matcher.resolve_to_command();
	if (result) {
		std::println("{}", *result);
	} else {
		std::println("Failed to resolve symbol {} required from {}", Symbol::demangled_name(result.error().symbol), result.error().origin);
	}
}

int main(int argc, char *argv[]) {
#ifndef _NDEBUG
	test();
#endif
	boost::program_options::options_description options(
		"libfinder finds the libraries that define a given symbol.\nRun 'sudo updatedb' to make sure all libs are locatable, create an index with 'libfinder "
		"-u' (once every time your libs change) and look up a symbol with 'libfinder -s [symbol]' to get a list of libraries that define "
		"[symbol].\nParameters");
	int jobs = 0;
	const int hardware_concurrency = std::thread::hardware_concurrency();
	const auto update_description =
		"update lookup table (must be done before first use) with given number of threads (default=" + std::to_string(hardware_concurrency) + ")";
	options.add_options()																										  //
		("help,h", "print this")																								  //
		("print,p", "print test")																								  //
		("update,u", boost::program_options::value<int>(&jobs)->implicit_value(hardware_concurrency), update_description.c_str()) //
		("symbol,s", boost::program_options::value<std::string>(), "the symbol to look up")										  //
		("linkcommand,c", boost::program_options::value<std::vector<std::string>>(), "the files to link")						  //
		;
	//("output-format,of", boost::program_options::value<std::string>()->default_value("symbol-list"), "Define the output format. Options are \tlist - pr");
	boost::program_options::variables_map program_args;
	try {
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), program_args);
	} catch (const boost::program_options::error &e) {
		std::cout << "error: " << e.what() << '\n' << "run '" << argv[0] << " --help' for details\n";
		return -1;
	}
	boost::program_options::notify(program_args);
	if (program_args.count("print")) {
		return 0;
	}
	if (program_args.count("help")) {
		std::cout << options;
	}
	if (program_args.count("update")) {
		if (jobs < 1) {
			std::cerr << "jobs must be at least 1\n";
			return -1;
		}
		update(jobs);
	}
	if (program_args.count("symbol")) {
		const auto &prefix = program_args["symbol"].as<std::string>();
		std::cout << "All symbols that have the prefix \"" << prefix << "\" and their libraries:\n";
		auto symbols = prefix_lookup(prefix);
		for (auto &symbol : symbols) {
			std::cout << symbol.get_symbol() << '\n';
			auto libs = symbol.get_libs_view();
			std::sort(std::begin(libs), std::end(libs));
			libs.erase(std::unique(std::begin(libs), std::end(libs)), std::end(libs));
			for (auto &lib : libs) {
				std::cout << '\t' << lib << '\n';
			}
		}
		return 0; //avoid double newline at end of output
	}
	if (program_args.count("linkcommand")) {
		std::vector<std::string_view> files;
		files.resize(argc - 2);
		for (int i = 2; i < argc; i++) {
			files[i - 2] = argv[i];
		}
		handle_linkcommand(files);
		return 0;
	}
	std::cout << '\n';
}
