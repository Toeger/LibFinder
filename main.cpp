#include "main.h"
#include "generate.h"
#include "libparser.h"
#include "profile.h"
#include "symbol_database.h"
#include "symbol_matcher.h"
#include "test.h"
#include "utility.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <expected>
#include <format>
#include <iostream>
#include <print>
#include <ranges>
#include <thread>

const std::string data_base_path = [] {
	auto username = get_output_from_command("whoami", {});
	username.pop_back(); //remove newline
	return "/home/" + username + "/.libfinder";
}();

//TODO: find a way to share the files between users
const std::string data_base_filepath = data_base_path + "/database";

static void handle_linkcommand(std::span<std::filesystem::path> files) {
	for (auto &file : files) {
		file = std::filesystem::current_path() / file;
		if (not std::filesystem::exists(file)) {
			throw std::runtime_error{std::format("Error: file not found: {}", file.c_str())};
		}
	}

	Symbol_matcher symbol_matcher{data_base_filepath};
	symbol_matcher.load_compile_commands_json("/home/toeger/Projects/LibFinder/test/compile_commands.json");
	for (auto &file : files) {
		for (auto &symbol : parse_lib(file, true, {})) { //TODO: Pass proper library dirs
			if (symbol.name != "_GLOBAL_OFFSET_TABLE_") {
				symbol_matcher.add(symbol, file);
			}
		}
	}
	std::println("{} defined and {} undefined symbols found before linking", symbol_matcher.defined.size(), symbol_matcher.undefined.size());
	auto result = symbol_matcher.resolve_to_command();
	std::println("{}", result);
}

int main(int argc, char *argv[]) try {
	boost::program_options::options_description options(
		"libfinder finds the libraries that define a given symbol.\nRun 'sudo updatedb' to make sure all libs are locatable, create an index with "
		"'libfinder "
		"-u' (once every time your libs change) and look up a symbol with 'libfinder -s [symbol]' to get a list of libraries that define "
		"[symbol].\nParameters");
	int jobs = 0;
	const int hardware_concurrency = std::thread::hardware_concurrency();
	const auto update_description =
		"update lookup table (must be done before first use) with given number of threads (default=" + std::to_string(hardware_concurrency) + ")";
	options.add_options()																										  //
		("help,h", "print this")																								  //
		("test,t", "test")																										  //
		("profile,p", "Print timing measurements")																				  //
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
	if (program_args.contains("test")) {
		for (auto &symbol : parse_lib("/usr/lib/x86_64-linux-gnu/libc.so", false, {})) {
			std::cout << symbol.name << '\n';
		}
		return 0;
	}
	if (program_args.contains("profile")) {
		profile::target = &std::cerr;
	}
	if (program_args.contains("help")) {
		std::cout << options;
	}
	if (program_args.contains("update")) {
		if (jobs < 1) {
			std::cerr << "jobs must be at least 1\n";
			return -1;
		}
		update(jobs);
	}
	if (program_args.contains("symbol")) {
		const auto &prefix = program_args["symbol"].as<std::string>();
		std::cout << "All symbols that have the prefix \"" << prefix << "\" and their libraries:\n";
		Symbol_database::Reader reader{data_base_filepath};
		auto symbols_libs = reader.libraries_from_prefix(prefix);
		for (auto &[symbol, libs] : symbols_libs) {
			std::cout << symbol << '\n';
			std::sort(std::begin(libs), std::end(libs));
			libs.erase(std::unique(std::begin(libs), std::end(libs)), std::end(libs));
			for (auto &lib : libs) {
				std::cout << '\t' << lib.string() << '\n';
			}
		}
		return 0; //avoid double newline at end of output
	}
	if (program_args.contains("linkcommand")) {
		std::vector<std::filesystem::path> files;
		files.resize(argc - 2);
		for (int i = 2; i < argc; i++) {
			files[i - 2] = argv[i];
		}
		handle_linkcommand(files);
		return 0;
	}
	std::cout << '\n';
} catch (std::runtime_error &error) {
	std::println(stderr, "{}", error.what());
}
