#include "thread_safe_queue.h"

#include <algorithm>
#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/program_options.hpp>
#include <cassert>
#include <experimental/string_view>
#include <fstream>
#include <future>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>

using Map = std::map<std::string, std::string>;

static std::atomic<int> symbols{0};

static std::mutex popen_mutex;

enum class Search_type { exact, prefix };

using string_view = std::experimental::string_view;

static const auto entry_separator = '\0'; //must be illegal in symbol names and paths
static const auto file_separator = '\1';  //must be illegal in paths

template <class T>
struct Sorted_Vector_Adapter {
	//insert objects into a vector while keeping it sorted
	Sorted_Vector_Adapter(std::vector<T> &v)
		: v(v) {}
	template <class... Args>
	void insert(Args... args) {
		T t{args...};
		auto it = std::lower_bound(std::begin(v), std::end(v), t);
		if (it == std::end(v) || *it != t) {
			v.insert(it, std::move(t));
		}
	}

	private:
	std::vector<T> &v;
};

template <class T>
struct Sorted_Vector_Adapter<T> make_sorted_vector_adapter(std::vector<T> &v) {
	return {v};
}

std::string
get_output_from_command(string_view command) {
	assert(command.data());
	std::unique_ptr<FILE, decltype(pclose) *> fp{nullptr, &pclose};
	{
		std::lock_guard<std::mutex> popen_lock(popen_mutex); //unfortunately popen doesn't seem to be thread safe
		std::unique_ptr<FILE, decltype(pclose) *> p{popen(command.data(), "r"), &pclose};
		fp = std::move(p);
	}
	if (!fp) {
		return {};
	}
	std::string buffer;
	const int buffersize = 1024;
	for (;;) {
		buffer.resize(buffer.size() + buffersize);
		std::size_t read = fread(&buffer[buffer.size() - buffersize], sizeof *buffer.data(), buffersize, fp.get());
		if (read < buffersize) {
			buffer.resize(buffer.size() - buffersize + read);
			break;
		}
	}
	return buffer;
}

static const auto data_base_path = [] {
	auto username = get_output_from_command("whoami");
	username.pop_back(); //remove newline
	return "/home/" + username + "/.libfinder";
}();
static const auto data_base_file = data_base_path + "/database"; //TODO: find a way to share the database between users

void add_to_database(Map &symbol_file_map, const string_view file_path) {
	std::stringstream ss(get_output_from_command(std::string("objdump -TCw  ") + file_path.data()));
	std::string line;
	std::getline(ss, line); //empty line
	std::getline(ss, line); //file format
	std::getline(ss, line); //empty line
	std::getline(ss, line); //caption
	std::getline(ss, line); //.init line
	auto linit = line.find(".init");
	if (linit == std::string::npos) {
		return;
	}
	auto rinit = line.rfind(".init");
	if (rinit == std::string::npos) {
		return;
	}
	while (std::getline(ss, line)) {
		if (string_view(line.data() + linit, 5) != ".text") {
			continue;
		}
		auto symbol_pos = line.data() + rinit - 1;
		while (*symbol_pos++ != ' ') {
		}
		auto &entry = symbol_file_map[symbol_pos];
		entry.push_back(file_separator);
		entry += file_path.data();
		symbols++;
	}
}

const char *symbol_lookup(string_view data, string_view symbol, Search_type st) {
	auto clean_pos = [](const char *&data) {
		while (data[-1] != entry_separator) {
			data--;
		}
	};

	auto estimate_position = [](const char *left, const char *right, const char *to_estimate) {
		while (*left == *right) {
			left++;
			right++;
			to_estimate++;
		}
		auto get_value = [](const char *p) {
			auto get_number = [](char c) -> int { return std::min(std::max(c - ' ', 0), 'z' + 0); };
			return get_number(p[0]) * 90 * 90 + get_number(p[1]) * 90 + get_number(p[2]);
		};

		auto left_value = get_value(left);
		auto right_value = get_value(right);
		auto estimate_value = get_value(to_estimate);
		assert(left_value <= estimate_value);
		assert(estimate_value <= right_value);
		if (left_value == right_value) {
			return left;
		}
		return left + (right - left) * (estimate_value - left_value) / (right_value - left_value);
	};

	auto left = data.data() + 1;
	auto right = data.data() + data.size() - 1;
	clean_pos(right);
	while (right - left > 1000) {
		auto mid = estimate_position(left, right, symbol.data());
		clean_pos(mid);
		if (symbol < mid) {
			right = mid;
		} else {
			left = mid;
		}
		//add a binary search component to prevent O(n) degradation
		mid = left + (right - left) / 2;
		clean_pos(mid);
		if (symbol < mid) {
			right = mid;
		} else {
			left = mid;
		}
	}
	while (*right++ != file_separator) {
	}
	string_view rest(left, right - left);
	auto pos = rest.find(symbol);
	if (pos == rest.npos) {
		//didn't find it
		return nullptr;
	}
	auto result = rest.data() + pos;
	if (result[symbol.size()] != file_separator) { //found a prefix
		if (st != Search_type::prefix) {
			return nullptr;
		}
	}
	return result;
}

std::vector<std::string> lookup(string_view symbol, Search_type st) {
	std::vector<std::string> retval;
	boost::interprocess::file_mapping file(data_base_file.c_str(), boost::interprocess::read_only);
	boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
	string_view data(static_cast<const char *>(region.get_address()), region.get_size());
	auto files = symbol_lookup(data, symbol, st);
	if (files) {
		auto sva = make_sorted_vector_adapter(retval);
		auto save_files = [&sva](const char *files) {
			while (*files++ != file_separator) {
			}
			auto files_end = files;
			while (*++files_end != entry_separator) {
			}
			string_view all_files(files, files_end - files);
			for (auto pos = all_files.find(file_separator); pos != all_files.npos; pos = all_files.find(file_separator)) {
				sva.insert(all_files.data(), pos);
				all_files.remove_prefix(pos + 1);
			}
			sva.insert(all_files.data(), all_files.size());
			return files_end;
		};
		switch (st) {
			case Search_type::exact: {
				save_files(files);
				break;
			}
			case Search_type::prefix: {
				do {
					files = save_files(files) + 1;
				} while (string_view(files, symbol.size()) == symbol);
				break;
			}
		}
	}
	return retval;
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
	if (variables_map.count("update")) {
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
					add_to_database(symbol_map, lib_path);
					std::lock_guard<std::mutex> lock(printer);
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
		std::ofstream db_file(data_base_file, std::ios_base::out);
		if (!db_file) {
			std::cerr << "failed opening database file " << data_base_file << '\n';
			return -1;
		}
		for (auto &p : symbol_map) {
			db_file << entry_separator << p.first << p.second;
		}
	}
	if (variables_map.count("symbol")) {
		const auto &symbol = variables_map["symbol"].as<std::string>();
		std::cout << "All libraries that contain the exact symbol \"" << symbol << "\":\n";
		auto files = lookup(symbol, Search_type::exact);
		std::sort(std::begin(files), std::end(files));
		for (auto &file : files) {
			std::cout << file << '\n';
		}
		return 0; //avoid double newline at end of output
	}
	if (variables_map.count("prefix")) {
		const auto &prefix = variables_map["prefix"].as<std::string>();
		std::cout << "All libraries that contain the prefix \"" << prefix << "\" in any of their symbols:\n";
		auto files = lookup(prefix, Search_type::prefix);
		for (auto &file : files) {
			std::cout << file << '\n';
		}
		return 0; //avoid double newline at end of output
	}
	std::cout << '\n';
}