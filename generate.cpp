#include "generate.h"
#include "main.h"
#include "thread_safe_queue.h"
#include "utility.h"

#include <atomic>
#include <boost/filesystem.hpp>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string>

static int symbols;

int add_to_database_so(Map &symbol_file_map, const string_view file_path) {
	int symbols = 0;
	std::stringstream ss(get_output_from_command(std::string("objdump -TCw ") + file_path.data()));
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

int add_to_database_a(Map &symbol_file_map, const string_view file_path) {
	int symbols = 0;
	std::stringstream ss(get_output_from_command(std::string("nm -gC ") + file_path.data()));
	std::string line;
	while (std::getline(ss, line)) {
		if (line.size() < 20) { //if the line is too short to contain a symbol skip it
			continue;
		}
		if (string_view(line.c_str() + 16, 3) == " U ") { //if the line refers to an undefined symbol skip it
			continue;
		}

		auto &entry = symbol_file_map[line.c_str() + 19];
		entry.push_back(file_separator);
		entry += file_path.data();
		symbols++;
	}
	return symbols;
}

void update(int jobs) {
	std::cout << "Generating file list\n" << std::flush;
	Thread_safe_queue<std::string> so_file_paths;
	Thread_safe_queue<std::string> a_file_paths;
	std::map<std::string, std::string> symbolic_links;
	std::atomic<int> libs{0};
	int symbol_counter = 0;
	{
		//add all .so files to queue
		auto &queue = so_file_paths.not_thread_safe_get();
		std::istringstream is(get_output_from_command(R"(locate -ber \.so$)"));
		for (std::string line; std::getline(is, line);) {
			auto file_type = get_output_from_command("file " + line);
			if (file_type.find("ELF 64-bit LSB shared object") != std::string::npos) {
				queue.push(std::move(line));
				std::cout << ++symbol_counter << '\r' << std::flush;
			} else if (file_type.find("symbolic link")) {
				symbolic_links[get_output_from_command("readlink -f " + line)] += file_separator + line;
			}
			//else skip
		}
	}
	{
		//add all .a files to queue
		auto &queue = a_file_paths.not_thread_safe_get();
		std::istringstream is(get_output_from_command(R"(locate -ber \.a$)"));
		for (std::string line; std::getline(is, line);) {
			auto file_type = get_output_from_command("file " + line);
			if (file_type.find("current ar archive") != std::string::npos) {
				queue.push(std::move(line));
				std::cout << ++symbol_counter << '\r' << std::flush;
			} else if (file_type.find("symbolic link")) {
				symbolic_links[get_output_from_command("readlink -f " + line)] += file_separator + line;
			}
			//else skip
		}
	}
	{
		//create symbolic link file
		std::ofstream symbolic_links_file(symbolic_links_filepath, std::ios_base::out | std::ios::binary);
		std::ofstream symbolic_links_index_file(symbolic_links_index_filepath, std::ios_base::out | std::ios::binary);
		for (const auto &link : symbolic_links) {
			File_index_t index = symbolic_links_file.tellp();
			symbolic_links_index_file.write(any_cast<const char *>(&index), sizeof index);
			symbolic_links_file << link.first << link.second << entry_separator;
		}
		assert(symbolic_links_file);
		assert(symbolic_links_index_file);
	}
	const int total_libs = so_file_paths.size() + a_file_paths.size();

	std::vector<std::future<Map>> threads;
	std::mutex printer;

	//function for each thread to execute, which takes a chunk of paths to scan from the queue and scans them until the queue is empty
	auto thread_handler = [&printer, &so_file_paths, &a_file_paths, &libs, total_libs] {
		Map symbol_map;
		while (!so_file_paths.empty()) {
			auto lib_paths = so_file_paths.pop_n(100);
			for (auto &lib_path : lib_paths) {
				auto added_symbols = add_to_database_so(symbol_map, lib_path);
				std::lock_guard<std::mutex> lock(printer);
				symbols += added_symbols;
				std::cout << "found " << symbols << " symbols in " << ++libs << "/" << total_libs << " libs\r" << std::flush;
			}
		}
		while (!a_file_paths.empty()) {
			auto lib_paths = a_file_paths.pop_n(100);
			for (auto &lib_path : lib_paths) {
				auto added_symbols = add_to_database_a(symbol_map, lib_path);
				std::lock_guard<std::mutex> lock(printer);
				symbols += added_symbols;
				std::cout << "found " << symbols << " symbols in " << ++libs << "/" << total_libs << " libs\r" << std::flush;
			}
		}
		return symbol_map;
	};
	//create threads
	threads.reserve(jobs - 1);
	std::generate_n(std::back_inserter(threads), jobs - 1, [&thread_handler] { return std::async(std::launch::async, thread_handler); });
	auto symbol_map = thread_handler();
	//collect results
	for (auto &t : threads) {
		for (auto &p : t.get()) {
			symbol_map[p.first] += p.second;
		}
	}

	//write results to disk
	std::cout << '\n' << "writing results to file " << data_base_filepath << std::flush;
	boost::filesystem::create_directory(data_base_path);
	std::ofstream db_file(data_base_filepath, std::ios_base::out | std::ios::binary);
	std::ofstream index_file(data_base_index_filepath, std::ios_base::out | std::ios::binary);
	for (auto &p : symbol_map) {
		File_index_t index = db_file.tellp();
		index_file.write(any_cast<const char *>(&index), sizeof index);
		db_file << p.first << p.second << entry_separator;
	}
	assert(index_file);
	assert(db_file);
}
