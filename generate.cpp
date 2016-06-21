#include "generate.h"
#include "main.h"
#include "thread_safe_queue.h"
#include "utility.h"

#include <atomic>
#include <boost/filesystem.hpp>
#include <cassert>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string>

static int symbols;

int add_to_database(Map &symbol_file_map, const string_view file_path) {
	int symbols = 0;
	std::stringstream ss(get_output_from_command(std::string("objdump -TCw  ") + file_path.data()));
	std::string line;
	std::getline(ss, line); //empty line
	std::getline(ss, line); //file format
	std::getline(ss, line); //empty line
	std::getline(ss, line); //caption
	std::getline(ss, line); //.init line
	auto linit = line.find(".init");
	if (linit == std::string::npos) {
		return symbols;
	}
	auto rinit = line.rfind(".init");
	if (rinit == std::string::npos) {
		return symbols;
	}
	while (std::getline(ss, line)) {
		if (string_view(line.data() + linit, 5) == "*UND*") {
			continue;
		}
		auto symbol_pos = line.data() + rinit - 3;
		while (*symbol_pos++ != ' ') {
		}
		while (*symbol_pos == ' ') {
			symbol_pos++;
		}
		auto &entry = symbol_file_map[symbol_pos];
		entry.push_back(file_separator);
		entry += file_path.data();
		symbols++;
	}
	return symbols;
}

void update(int jobs) {
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
