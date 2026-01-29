#include "generate.h"
#include "libparser.h"
#include "main.h"
#include "symbol_database.h"
#include "utility.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <future>
#include <iostream>
#include <sstream>
#include <string>

void update(int jobs) {
	std::cout << "Collecting library candidates...\r" << std::flush;
	std::atomic<std::size_t> handled_libs{0};
	std::size_t library_candidates;
	const std::vector<std::string> file_paths = [&library_candidates] {
		std::vector<std::string> queue;
		//add all lib*.so files to queue
		{
			std::istringstream is(get_output_from_command(R"(locate -ber lib.*\.so$)").output);
			for (std::string line; std::getline(is, line);) {
				queue.push_back(std::move(line));
			}
		}
		{
			std::istringstream is(get_output_from_command(R"(locate -ber lib.*\.a$)").output);
			for (std::string line; std::getline(is, line);) {
				queue.push_back(std::move(line));
			}
		}
		//file_paths.pop_n(queue.size() - 100);
		library_candidates = queue.size();
		std::cout << "Collecting symbols from " << library_candidates << " library candidates..." << std::endl;
		return queue;
	}();

	//function for each thread to execute, which takes a chunk of paths to scan from the queue and scans them until the queue is empty
	auto thread_handler = [&file_paths, &handled_libs, library_candidates] {
		Symbol_database::Writer symbol_map;
		for (std::size_t lib_index = handled_libs++; lib_index < std::size(file_paths); lib_index = handled_libs++) {
			if (lib_index * 100 / library_candidates > ((lib_index - 1) * 100) / library_candidates) {
				std::cout << lib_index * 100 / library_candidates << "%\r" << std::flush;
			}
			auto file_type = get_output_from_command("file \"" + file_paths[lib_index] + '"').output;
			if (file_type.contains("symbolic link")) {
				auto target = get_output_from_command("readlink -f \"" + file_paths[lib_index] + '"').output;
				target.pop_back();
				file_type = get_output_from_command("file \"" + target + '"').output;
			}
			bool is_shared_object = file_type.contains("ELF 64-bit LSB shared object");
			if (not is_shared_object and not file_type.contains("current ar archive")) {
				continue;
			}
			for (auto &symbol : parse_lib(file_paths[lib_index], is_shared_object)) {
				if (symbol.type == Symbol_type::undefined) {
					continue;
				}
				symbol_map.add(symbol.name, file_paths[lib_index]);
			}
		}
		return symbol_map;
	};

	std::vector<std::future<Symbol_database::Writer>> threads;
	std::generate_n(std::back_inserter(threads), jobs - 1, [&thread_handler] { return std::async(std::launch::async, thread_handler); });
	auto symbol_map = thread_handler();
	for (auto &thread : threads) {
		symbol_map.merge(thread.get());
	}

	//write results to disk
	std::cout << "100%\nwriting " << symbol_map.size() << " symbols to " << data_base_filepath << std::endl;
	const auto stats = symbol_map.write(data_base_filepath);
	auto bytes = [](std::size_t bytes) {
		constexpr const char *units[] = {"B", "kB", "MB", "GB"};
		std::size_t unit = 1;
		while (bytes >= 100000) {
			bytes /= 1024;
			unit++;
		}
		return std::format("{:.1f}{}", bytes / 1024., units[unit]);
	};
	std::cout << "Wrote " << stats.unique_symbols << " unique symbols into " << bytes(stats.symbols_db_size) << " of database with an index of "
			  << bytes(stats.symbols_index_size) << "\n";
	std::cout << "Wrote " << stats.unique_libs << " unique libs into " << bytes(stats.libs_db_size) << " of database with an index of "
			  << bytes(stats.libs_index_size) << "\n";
}
