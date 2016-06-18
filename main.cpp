#include "main.h"
#include "generate.h"
#include "lookup.h"
#include "thread_safe_queue.h"
#include "utility.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <future>
#include <iostream>

static int symbols;

static const auto data_base_path = [] {
	auto username = get_output_from_command("whoami");
	username.pop_back(); //remove newline
	return "/home/" + username + "/.libfinder";
}();

//TODO: find a way to share the files between users
const std::string data_base_filepath = data_base_path + "/database";
const std::string index_filepath = data_base_path + "/database_index";

static void update(int jobs){
	Thread_safe_queue<std::string> lib_file_paths;
	std::atomic<int> libs{0};
	{
		auto &queue = lib_file_paths.not_thread_safe_get();
		std::istringstream is(get_output_from_command("locate .so"));
		for (std::string line; std::getline(is, line);) {
			if (line.rfind('/') < line.rfind(".so")) {
				queue.push(std::move(line));
			}
		}
	}
	const int total_libs = lib_file_paths.size();

	std::vector<std::future<Map>> threads;
	std::mutex printer;

	auto thread_handler = [&printer, &lib_file_paths, &libs, total_libs] {
		Map symbol_map;
		while (!lib_file_paths.empty()) {
			auto lib_paths = lib_file_paths.pop_n(100);
			for (auto &lib_path : lib_paths) {
				auto added_symbols = add_to_database(symbol_map, lib_path);
				std::lock_guard<std::mutex> lock(printer);
				symbols += added_symbols;
				std::cout << "found " << symbols << " symbols in " << ++libs << "/" << total_libs << " libs\r" << std::flush;
			}
		}
		return symbol_map;
	};
	threads.reserve(jobs - 1);
	while (--jobs) {
		threads.push_back(std::async(std::launch::async, thread_handler));
	}
	auto symbol_map = thread_handler();
	for (auto &t : threads) {
		for (auto &p : t.get()) {
			symbol_map[p.first] += p.second;
		}
	}

	boost::filesystem::create_directory(data_base_path);
	std::ofstream db_file(data_base_filepath, std::ios_base::out | std::ios::binary);
	std::ofstream index_file(index_filepath, std::ios_base::out | std::ios::binary);
	if (!db_file) {
		std::cerr << "failed opening database file " << data_base_filepath;
		return;
	}
	for (auto &p : symbol_map) {
		int index = db_file.tellp();
		index_file.write(any_cast<const char *>(&index), sizeof index);
		db_file << p.first << p.second << entry_separator;
	}
}

int main(int argc, char *argv[]) {
	boost::program_options::options_description options(
		"libfinder finds the libraries that define a given symbol.\nRun 'sudo updatedb' to make sure all libs are locatable, create an index with "
		"'libfinder "
		"-u' (once every time your libs change) and look up a symbol with 'libfinder -s [symbol]' to get a list of libraries that define "
		"[symbol].\nParameters");
	int jobs = 0;
	options.add_options()("help,h", "print this")("update,u", "update lookup table")("symbol,s", boost::program_options::value<std::string>(),
																					 "the symbol to look up")(
		"prefix,p", boost::program_options::value<std::string>(), "find libraries that have a symbol starting with the given argument")(
		"jobs,j", boost::program_options::value<int>(&jobs)->default_value(1), "specify the maximum number of threads to use, must be at least 1");
	boost::program_options::variables_map variables_map;
	try {
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), variables_map);
	} catch (const boost::program_options::error &e) {
		std::cout << "error: " << e.what() << '\n' << "run '" << argv[0] << " --help' for details\n";
		return -1;
	}
	boost::program_options::notify(variables_map);
	if (jobs < 1) {
		std::cerr << "jobs must be at least 1\n";
		return -1;
	}
	if (variables_map.count("help")) {
		std::cout << options;
	}
	if (variables_map.count("update"))
	{
		update(jobs);
	}
	if (variables_map.count("symbol")) {
		const auto &symbol = variables_map["symbol"].as<std::string>();
		std::cout << "All libraries that contain the exact symbol \"" << symbol << "\":\n";
		auto files = exact_lookup(symbol);
		std::sort(std::begin(files), std::end(files));
		for (auto &file : files) {
			std::cout << file << '\n';
		}
		return 0; //avoid double newline at end of output
	}
	if (variables_map.count("prefix")) {
		const auto &prefix = variables_map["prefix"].as<std::string>();
		std::cout << "All libraries that contain the prefix \"" << prefix << "\" in any of their symbols:\n";
		auto symbols = prefix_lookup(prefix);
		for (auto &symbol : symbols) {
			std::cout << symbol.get_symbol() << '\n';
			auto libs = symbol.get_libs_view();
			std::sort(std::begin(libs), std::end(libs));
			for (auto &lib : libs){
				std::cout << '\t' << lib << '\n';
			}
		}
		return 0; //avoid double newline at end of output
	}
	std::cout << '\n';
}