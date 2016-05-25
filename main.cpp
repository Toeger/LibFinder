#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/program_options.hpp>
#include <boost/scope_exit.hpp>
#include <cassert>
#include <experimental/string_view>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>

static const auto libdirs = {"/lib", "/usr/lib"};
static const auto data_base_file = "~/.libfinder/database"; //TODO: find a way to share the database between users

std::string get_output_from_command(std::experimental::string_view command) {
	auto fp = popen(command.data(), "r");
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

void add_to_database(std::map<std::string, std::string> &symbol_file_map, const std::experimental::string_view dir) {
	for (auto &file : boost::filesystem::recursive_directory_iterator(boost::filesystem::path(dir.data()))) {
		auto file_command_output = get_output_from_command("file -L " + file.path().string());
		if (file_command_output.find("symbolic link to ") != std::string::npos) {
			continue;
		}
		if (file_command_output.find("ELF 32-bit LSB shared object") != std::string::npos) {
			continue;
		}
		if (file_command_output.find("ELF 64-bit LSB shared object") == std::string::npos) {
			continue;
		}
		std::stringstream ss(get_output_from_command("objdump -TCw  " + file.path().string()));
		std::string line;
		std::getline(ss, line); //empty line
		std::getline(ss, line); //file format
		std::getline(ss, line); //empty line
		std::getline(ss, line); //caption
		std::getline(ss, line); //.init line
		auto linit = line.find(".init");
		if (linit == std::string::npos) {
			continue;
		}
		auto rinit = line.rfind(".init");
		if (rinit == std::string::npos) {
			continue;
		}
		while (std::getline(ss, line)) {
			if (std::experimental::string_view(line.data() + linit, 5) != ".text") {
				continue;
			}
			auto symbol_pos = line.data() + rinit - 1;
			while (*symbol_pos++ != ' ') {
				;
			}
			symbol_file_map[symbol_pos] += ':' + file.path().string();
		}
	}
}

template <class T>
void create_database(std::ostream &file, const T &library_directories) {
	std::map<std::string, std::string> symbol_file_map;
	for (auto &dir : library_directories) {
		add_to_database(symbol_file_map, dir);
	}
	for (auto &p : symbol_file_map) {
		file << '$' << p.first << p.second;
	}
}

const char *find_files(std::experimental::string_view data, std::experimental::string_view symbol) {
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
	boost::interprocess::file_mapping file(data_base_file, boost::interprocess::read_only);
	boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
	std::experimental::string_view data(static_cast<const char *>(region.get_address()), region.get_size());
	auto files = find_files(data, symbol);
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
	options.add_options()("help,h", "print this")("update,u", "update lookup table")("symbol,s", boost::program_options::value<std::string>(),
																					 "the symbol to look up");
	boost::program_options::variables_map variables_map;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), variables_map);
	boost::program_options::notify(variables_map);
	if (variables_map.count("update")) {
		std::ofstream db_file(data_base_file);
		create_database(db_file, libdirs);
	} if (variables_map.count("help")) {
		std::cout << options;
	} if (variables_map.count("symbol")) {
		const auto &files = lookup(variables_map["symbol"].as<std::string>());
		for (auto &file : files) {
			std::cout << file << '\n';
			break;
		}
	}
}