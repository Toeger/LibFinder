#include "generate.h"
#include "main.h"
#include "utility.h"

#include <sstream>
#include <string>
#include <cassert>

int add_to_database(Map &symbol_file_map, const string_view file_path) {
	int symbols = 0;
	std::stringstream ss(get_output_from_command(std::string("objdump -TCw  ") + file_path.data()));
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
		auto symbol_pos = line.data() + rinit - 2;
		while (*symbol_pos++ != ' ') {
		}
		while (*symbol_pos == ' '){
			symbol_pos++;
		}
		auto &entry = symbol_file_map[symbol_pos];
		entry.push_back(file_separator);
		entry += file_path.data();
		symbols++;
	}
	return symbols;
}
