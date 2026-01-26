#include "generate.h"
#include "libparser.h"
#include "main.h"
#include "thread_safe_queue.h"
#include "utility.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

void update(int jobs) {
	std::cout << "Collecting library candidates...\r" << std::flush;
	Thread_safe_queue<std::string> file_paths;
	std::atomic<std::size_t> handled_libs{0};
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
		//file_paths.pop_n(queue.size() - 100);
		library_candidates = queue.size();
		std::cout << "Collecting symbols from " << library_candidates << " library candidates..." << std::endl;
	}

	std::vector<std::future<Map>> threads;

	//function for each thread to execute, which takes a chunk of paths to scan from the queue and scans them until the queue is empty
	auto thread_handler = [&file_paths, &handled_libs, library_candidates] {
		Map symbol_map;
		while (not file_paths.empty()) {
			auto lib_paths = file_paths.pop_n(handled_libs * 100 / library_candidates < 90 ? 100 : 10);
			for (auto &lib_path : lib_paths) {
				auto n = ++handled_libs;
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
	std::cout << '\n' << "writing " << symbol_map.size() << " symbols to " << data_base_path << std::flush;
	std::ofstream db_file(data_base_filepath, std::ios_base::out | std::ios::binary);
	std::ofstream index_file(data_base_index_filepath, std::ios_base::out | std::ios::binary);
#if USING_OLD_STYLE
	for (auto &[symbol, libs] : symbol_map) {
		File_index_t index = db_file.tellp();
		index_file.write(reinterpret_cast<const char *>(&index), sizeof index);
		db_file << symbol << libs << entry_separator;
	}
#else

	struct Path_list {
		std::size_t path_index(const std::string &path) {
			auto &pos = data[path];
			if (pos == 0) {
				pos = size;
				size += path.size() + 1;
			}
			return pos;
		}
		void write(std::filesystem::path path) {
			std::map<std::size_t, std::string> rev_data;
			for (auto &[lib_path, pos] : data) {
				rev_data.insert({pos, lib_path});
			}
			std::ofstream file{path, std::ios_base::out | std::ios::binary};
			for (auto &[_, lib_path] : rev_data) {
				file << lib_path << '\n';
			}
		}
		std::map<std::string, std::size_t> data;
		std::size_t size;
	} path_list;

	auto write_index = [](std::ostream &os, std::size_t index, int bytes) {
		while (bytes--) {
			os << static_cast<std::uint8_t>(index & 0xff);
			index >>= 8;
		}
	};

	for (auto &[symbol, libs] : symbol_map) {
		File_index_t index = db_file.tellp();
		index_file.write(reinterpret_cast<const char *>(&index), sizeof index);
		db_file << symbol;
		for (auto lib : libs | std::ranges::views::split(file_separator) | std::views::drop(1)) {
			auto path = std::string_view{lib};
			auto pos = path.rfind(std::filesystem::path::preferred_separator);
			auto file = path;
			path.remove_suffix(path.size() - pos);
			file.remove_prefix(pos + 1);
			path_list.path_index(std::string{path});
			write_index(db_file, path_list.path_index(std::string{path}), 3);
			db_file << file << file_separator;
		}
		db_file << entry_separator;
	}
	path_list.write(data_base_path + std::filesystem::path::preferred_separator + "paths");
#endif
	assert(index_file.flush());
	assert(db_file.flush());
}
