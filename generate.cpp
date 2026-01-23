#include "generate.h"
#include "libparser.h"
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

void update(int jobs) {
	std::cout << "Collecting library candidates...\r" << std::flush;
	Thread_safe_queue<std::string> file_paths;
	std::atomic<std::size_t> libs{0};
	std::size_t library_candidates;
	{
		//add all lib*.so files to queue
		auto &queue = file_paths.not_thread_safe_get();
		{
			std::istringstream is(get_output_from_command(R"(locate -ber lib.*\.so$)"));
			for (std::string line; std::getline(is, line);) {
				queue.push(std::move(line));
			}
		}
		{
			std::istringstream is(get_output_from_command(R"(locate -ber lib.*\.a$)"));
			for (std::string line; std::getline(is, line);) {
				queue.push(std::move(line));
			}
		}
		library_candidates = queue.size();
		std::cout << "Collecting symbols from " << library_candidates << " library candidates..." << std::endl;
	}

	std::vector<std::future<Map>> threads;

	//function for each thread to execute, which takes a chunk of paths to scan from the queue and scans them until the queue is empty
	auto thread_handler = [&file_paths, &libs, library_candidates] {
		Map symbol_map;
		while (not file_paths.empty()) {
			auto lib_paths = file_paths.pop_n(libs * 100 / library_candidates < 90 ? 100 : 10);
			for (auto &lib_path : lib_paths) {
				auto n = ++libs;
				if (n * 100 / library_candidates > ((n - 1) * 100) / library_candidates) {
					std::cout << n * 100 / library_candidates << "%\r" << std::flush;
				}
				auto file_type = get_output_from_command("file \"" + lib_path + '"');
				if (file_type.contains("symbolic link")) {
					auto target = get_output_from_command("readlink -f \"" + lib_path + '"');
					target.pop_back();
					file_type = get_output_from_command("file \"" + target + '"');
				}
				bool is_shared_object = file_type.contains("ELF 64-bit LSB shared object");
				if (not is_shared_object and not file_type.contains("current ar archive")) {
					continue;
				}
				for (auto &symbol : parse_lib(lib_path, is_shared_object)) {
					if (symbol.type == Symbol_type::undefined) {
						continue;
					}
					symbol_map[symbol.name] += file_separator + lib_path;
				}
			}
		}
		return symbol_map;
	};

	std::move_only_function<Map(int)> thread_launcher = [&thread_handler, &thread_launcher](int njobs) {
		if (njobs == 1) {
			return thread_handler();
		}
		auto future_result = std::async(std::launch::async, std::ref(thread_launcher), njobs / 2);
		njobs -= njobs / 2;
		auto result = thread_launcher(njobs);
		for (auto &p : future_result.get()) {
			result[p.first] += std::move(p.second);
		}
		return result;
	};

	auto symbol_map = thread_launcher(jobs);

	//write results to disk
	std::cout << '\n' << "writing " << symbol_map.size() << " symbols to file " << data_base_filepath << std::flush;
	std::ofstream db_file(data_base_filepath, std::ios_base::out | std::ios::binary);
	std::ofstream index_file(data_base_index_filepath, std::ios_base::out | std::ios::binary);
	for (auto &[symbol, lib] : symbol_map) {
		File_index_t index = db_file.tellp();
		index_file.write(reinterpret_cast<const char *>(&index), sizeof index);
		db_file << symbol << lib << entry_separator;
	}
	assert(index_file.flush());
	assert(db_file.flush());
}
