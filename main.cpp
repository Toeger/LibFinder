#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/scope_exit.hpp>
#include <cassert>
#include <cstdio>
#include <experimental/string_view>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

std::string get_output(std::experimental::string_view view) {
	auto fp = popen(view.data(), "r");
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
		auto filetype = get_output("file " + file.path().string());
		if (filetype.find("ELF 64-bit LSB shared object") == std::string::npos) {
			continue;
		}
		std::stringstream ss(get_output("readelf -Ws " + file.path().string()));
		std::string line;
		std::getline(ss, line); //empty line
		std::getline(ss, line); //number of symbols in symbol table
		std::getline(ss, line); //caption
		auto name_pos = line.find("Name");
		while (std::getline(ss, line)) {
			if (line.data()[name_pos]) {
				line[name_pos - 1] = ':';
				symbol_file_map[line.data() + name_pos] += ':' + file.path().string();
			}
		}
	}
}

void create_database(std::ostream &file, const std::vector<std::string> &library_directories) {
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
		}
		auto get_value = [](const char *p) { return p[0] * 26 * 26 * 26 + p[1] * 26 * 26 + p[2] * 26 + p[3]; };

		auto left_value = get_value(left);
		auto right_value = get_value(right);
		auto estimate_value = get_value(to_estimate);
		assert(left_value <= estimate_value);
		assert(estimate_value <= right_value);
		return left + (right - left) * (estimate_value - left_value) / (right_value - left_value);
	};

	auto left = data.data() + 1;
	auto right = data.data() + data.size() - 1;
	clean_pos(right);
	while (right - left > 1000) {
		auto mid = estimate_position(left, right, symbol.data());
		clean_pos(mid);
		if (mid == left || mid == right) { //we made no progress
			break;
		}
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

const char *lookup(std::experimental::string_view symbol) {
	boost::interprocess::file_mapping file("/tmp/db", boost::interprocess::read_only);
	boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
	std::experimental::string_view data(static_cast<const char *>(region.get_address()), region.get_size());
	return find_files(data, symbol);
}

int main(int argc, char *argv[]) {
	const auto database_file = "/tmp/db";
	if (argc == 2) {
		if (std::strcmp(argv[1], "updatedb") == 0) {
			std::ofstream db_file(database_file);
			create_database(db_file, {"/usr/lib"});
		} else {
			auto files = lookup(argv[1]);
			if (files) {
				auto files_end = files;
				while (*files_end != '$') {
					files_end++;
				}
				std::cout << std::experimental::string_view(files, files_end - files) << '\n';
			}
		}
	} else {
		//TODO: print propper usage
		std::cout << "use \"LibFinder updatedb\" to update the library symbol index and then \"LibFinder [somesymbol]\" to look up the library to a symbol\n";
	}
}
