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
#include <print>
#include <sstream>
#include <string>
#include <utility>

void update(int jobs) {
	std::cout << "Collecting library candidates...\r" << std::flush;
	std::atomic<std::size_t> handled_libs{0};
	std::size_t library_candidates;
	const std::vector<std::string> file_paths = [&library_candidates] {
		std::vector<std::string> queue;
		//add all lib*.so files to queue
		{
			std::istringstream is(get_output_from_command("locate", {"-ber", R"(^lib.*\.so$)"}));
			for (std::string line; std::getline(is, line);) {
				assert(not line.empty());
				queue.push_back(std::move(line));
			}
		}
		{
			std::istringstream is(get_output_from_command("locate", {"-ber", R"(^lib.*\.a$)"}));
			for (std::string line; std::getline(is, line);) {
				assert(not line.empty());
				queue.push_back(std::move(line));
			}
		}
		//file_paths.pop_n(queue.size() - 100);
		library_candidates = queue.size();
		std::cout << "Collecting symbols from " << library_candidates << " library candidates..." << std::endl;
		return queue;
	}();

	//function for each thread to execute, which takes a chunk of paths to scan from the queue and scans them until the queue is empty
	auto thread_handler = [&file_paths = std::as_const(file_paths), &handled_libs, library_candidates = std::as_const(library_candidates)] {
		Symbol_database::Writer symbol_map;
		for (std::size_t lib_index = handled_libs++; lib_index < std::size(file_paths); lib_index = handled_libs++) {
			if (lib_index * 1000 / library_candidates > ((lib_index - 1) * 1000) / library_candidates) {
				std::print("{:.1f}%\r", (lib_index * 1000 / library_candidates) / 10.);
				std::cout << std::flush;
			}
			for (auto &symbol : parse_lib(file_paths[lib_index], file_paths[lib_index].ends_with(".so"))) {
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
	std::cout << "100%  \nwriting " << symbol_map.size() << " symbols to " << data_base_filepath << std::endl;
	const auto stats = symbol_map.write(data_base_filepath);
	auto bytes = [](std::size_t byte_count) {
		constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
		std::size_t unit = 0;
		double display_bytes = byte_count;
		while (display_bytes >= 10000) {
			display_bytes /= 1024;
			unit++;
		}
		return unit == 0 ? std::format("{}{}", display_bytes, **units) : std::format("{:.1f}{}", display_bytes, units[unit]);
	};
	std::cout << "Wrote " << stats.unique_symbols << " unique symbols into " << bytes(stats.symbols_db_size) << " of database with an index of "
			  << bytes(stats.symbols_index_size) << "\n";
	std::cout << "Wrote " << stats.unique_libs << " unique libs into " << bytes(stats.libs_db_size) << " of database with an index of "
			  << bytes(stats.libs_index_size) << "\n";
}
