#include "generate.h"
#include "format.h"
#include "libparser.h"
#include "main.h"
#include "profile.h"
#include "symbol_database.h"
#include "utility.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
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
	std::atomic<std::size_t> skipped_libs{0};
	std::size_t library_candidates;
	std::vector<std::string> file_paths = [&library_candidates] {
		std::vector<std::string> queue;
		//add all lib*.so and lib*.a files to queue
		std::istringstream is(get_output_from_command("locate", {"-m0bser", "--regextype", "awk", "^lib.*\\.(so|a)$"}));
		for (std::string line; std::getline(is, line, '\0');) {
			assert(not line.empty());
			assert(line.front() == '/');
			queue.push_back(std::move(line));
		}
		PROF << "to collect list of libraries\n";
		//file_paths.pop_n(queue.size() - 100);
		library_candidates = queue.size();
		std::cout << "Collecting symbols from " << number(library_candidates) << " library candidates...\n" << std::endl;
		return queue;
	}();

	PROF << "to find libraries";

	const auto &lib_paths = gcc_lib_paths();
	static auto print_status_update = [&library_candidates](std::size_t lib_index) {
		if (lib_index == 0) {
			return;
		}
		static std::atomic<decltype(std::chrono::high_resolution_clock::now())> last_update = std::chrono::high_resolution_clock::now();
		constexpr auto update_rate = std::chrono::seconds{1};
		auto now = std::chrono::high_resolution_clock::now();
		if (now < last_update.load() + update_rate) {
			return;
		}
		static std::mutex m;
		std::size_t last_updated_lib_index;
		std::size_t updates;
		std::size_t libs_left;
		{
			std::lock_guard lg{m};
			if (now < last_update.load() + update_rate) {
				return;
			}
			last_update = last_update.load() + std::chrono::seconds{1};
			static std::size_t old_lib_indexes[11] = {};
			libs_left = library_candidates - lib_index;
			std::shift_left(std::begin(old_lib_indexes), std::end(old_lib_indexes), 1);
			old_lib_indexes[std::size(old_lib_indexes) - 1] = lib_index;
			updates = std::size(old_lib_indexes) - 1;
			for (std::size_t i = 1; i < std::size(old_lib_indexes) - 1; i++) {
				if (old_lib_indexes[i]) {
					break;
				}
				updates--;
			}
			last_updated_lib_index = old_lib_indexes[std::size(old_lib_indexes) - 1 - updates];
		}
		const auto percentage = lib_index * 100. / library_candidates;
		std::print("\033[F\033[2K\033[G{:.2f}% ", percentage);
		auto estimate = 1. / (lib_index - last_updated_lib_index) * updates * 1. * libs_left;
		std::pair<int, char> conversions[] = {
			{0, 's'},
			{60, 'm'},
			{60, 'h'},
			{24, 'd'},
		};
		std::size_t conversion_index = 0;
		while (conversion_index + 1 < std::size(conversions)) {
			auto &next_conversion = conversions[conversion_index + 1];
			if (estimate > next_conversion.first) {
				estimate /= next_conversion.first;
				conversion_index++;
			} else {
				break;
			}
		}
		if (conversion_index == 0) {
			std::print("{:.0f}s", estimate);
		} else {
			std::print("{}{} {:.0f}{}", static_cast<int>(estimate), conversions[conversion_index].second,
					   std::fmod(estimate, 1) * conversions[conversion_index].first, conversions[conversion_index - 1].second);
		}
		std::cout << std::endl;
	};

	auto thread_handler = [&file_paths = std::as_const(file_paths), &handled_libs, library_candidates = std::as_const(library_candidates), &skipped_libs,
						   &lib_paths] {
		Symbol_database::Writer symbol_map{library_candidates};
		for (std::size_t lib_index = handled_libs++; lib_index < std::size(file_paths); lib_index = handled_libs++) {
			print_status_update(lib_index);
			auto libs = parse_lib(file_paths[lib_index], false, lib_paths);
			if (libs.empty()) {
				++skipped_libs;
				continue;
			}
			for (auto &symbol : libs) {
				symbol_map.add(std::move(symbol.mangled_name), lib_index);
			}
		}
		return symbol_map;
	};

	std::vector<std::future<Symbol_database::Writer>> threads;
	std::generate_n(std::back_inserter(threads), jobs - 1, [&thread_handler] { return std::async(std::launch::async, thread_handler); });
	auto symbol_map = thread_handler();

	PROF << "to parse libraries";
	std::cerr << '\n';

	for (auto &thread : threads) {
		symbol_map.merge(thread.get());
	}

	//write results to disk
	std::cout << "\033[F\033[2K\033[GFound " << number(symbol_map.size()) << " symbols in " << number(handled_libs - skipped_libs)
			  << " libraries, writing database..." << std::endl;
	const auto stats = symbol_map.write(data_base_filepath, std::move(file_paths));

	std::cout << "Writing result to " << data_base_filepath << "..." << std::endl;
	std::cout << "Wrote " << number(stats.unique_symbols) << " unique symbols into " << bytes(stats.symbols_db_size) << " of database with an index of "
			  << bytes(stats.symbols_index_size) << "\n";
	std::cout << "Wrote " << number(handled_libs) << " libraries into " << bytes(stats.libs_db_size) << " of database with an index of "
			  << bytes(stats.libs_index_size) << "\n";
}
