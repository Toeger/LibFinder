#include "thread_safe_queue.h"

#include <algorithm>
#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/program_options.hpp>
#include <boost/scope_exit.hpp>
#include <cassert>
#include <experimental/string_view>
#include <fstream>
#include <future>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>

using Map = std::map<std::string, std::string>;

static std::mutex popen_mutex;
std::string get_output_from_command(std::experimental::string_view command) {
	assert(command.data());
	FILE *fp; //maybe use a unique_ptr<FILE> instead of scope-exit?
	{
		std::lock_guard<std::mutex> popen_lock(popen_mutex); //unfortunately popen doesn't seem to be thread safe
		fp = popen(command.data(), "r");
	}
	if (!fp) {
		return {};
	}
	BOOST_SCOPE_EXIT(&fp) {
		pclose(fp);
	}
	BOOST_SCOPE_EXIT_END
	std::string buffer;
	const int buffersize = 1024;
	for (;;) {
		buffer.resize(buffer.size() + buffersize);
		std::size_t read = fread(&buffer[buffer.size() - buffersize], sizeof *buffer.data(), buffersize, fp);
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

static Thread_safe_queue<std::string> libs_to_scan;
static std::atomic<int> symbols{0};
static std::atomic<int> libs{0};
static std::atomic<int> active_threads;
int total_libs;

void add_to_database(Map &symbol_file_map, const std::experimental::string_view file_path) {
	libs++;
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
		if (std::experimental::string_view(line.data() + linit, 5) != ".text") {
			continue;
		}
		auto symbol_pos = line.data() + rinit - 1;
		while (*symbol_pos++ != ' ') {
		}
		auto &entry = symbol_file_map[symbol_pos];
		entry.push_back(':');
		entry += file_path.data();
		symbols++;
	}
}

const char *symbol_lookup(std::experimental::string_view data, std::experimental::string_view symbol) {
	auto clean_pos = [](const char *&data) {
		while (data[-1] != '$') {
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
	if (std::experimental::string_view(right, symbol.size()) == symbol) {
		return right + symbol.size() + 1;
	}
	std::experimental::string_view rest(left, right - left);
	auto pos = rest.find(symbol);
	if (pos == rest.npos) {
		//didn't find it
		return nullptr;
	}
	auto result = rest.data() + pos + symbol.size();
	if (*result != ':') {
		//just found a prefix
		return nullptr;
	}
	return result + 1;
}

std::vector<std::string> lookup(std::experimental::string_view symbol) {
	std::vector<std::string> retval;
	boost::interprocess::file_mapping file(data_base_file.c_str(), boost::interprocess::read_only);
	boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
	std::experimental::string_view data(static_cast<const char *>(region.get_address()), region.get_size());
	auto files = symbol_lookup(data, symbol);
	if (files) {
		auto files_end = files;
		while (*++files_end != '$') {
		}
		std::experimental::string_view all_files(files, files_end - files);
		for (auto pos = all_files.find(':'); pos != all_files.npos; pos = all_files.find(':')) {
			retval.emplace_back(all_files.data(), pos);
			all_files.remove_prefix(pos + 1);
		}
	}
	return retval;
}

int main(int argc, char *argv[]) {
	boost::program_options::options_description options("Parameters");
	int jobs = 0;
	options.add_options()("help,h", "print this")("update,u", "update lookup table")("symbol,s", boost::program_options::value<std::string>(),
																					 "the symbol to look up")(
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
		{
			auto &queue = lib_file_paths.not_thread_safe_get();
			std::istringstream is(get_output_from_command("locate .so"));
			for (std::string line; std::getline(is, line);) {
				queue.push(std::move(line));
			}
			total_libs = queue.size();
		}

		std::vector<std::future<Map>> threads;
		active_threads = jobs;
		std::mutex printer;

		auto thread_handler = [&printer, &lib_file_paths] {
			Map symbol_map;
			std::string lib;
			while (lib_file_paths.pop(lib)) {
				add_to_database(symbol_map, lib);
				std::lock_guard<std::mutex> lock(printer);
				std::cout << "found " << symbols << " symbols in " << libs << "/" << total_libs << " libs\r" << std::flush;
			}
			return symbol_map;
		};
		while (--jobs) {
			threads.push_back(std::async(std::launch::async, thread_handler));
		}
		auto symbol_map = thread_handler();
		{
			std::lock_guard<std::mutex> lock(printer);
			std::cout << "done scanning\n" << std::flush;
		}
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
			db_file << '$' << p.first << p.second;
		}
	}
	if (variables_map.count("symbol")) {
		const auto &files = lookup(variables_map["symbol"].as<std::string>());
		for (auto &file : files) {
			std::cout << file;
			break;
		}
	}
	std::cout << '\n';
}