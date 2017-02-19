#include "main.h"
#include "generate.h"
#include "lookup.h"
#include "test.h"
#include "utility.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <thread>

const std::string data_base_path = [] {
	auto username = get_output_from_command("whoami");
	username.pop_back(); //remove newline
	return "/home/" + username + "/.libfinder";
}();

//TODO: find a way to share the files between users
const std::string data_base_filepath = data_base_path + "/database";
const std::string data_base_index_filepath = data_base_path + "/database_index";
const std::string symbolic_links_filepath = data_base_path + "/links";
const std::string symbolic_links_index_filepath = data_base_path + "/links_index";

int main(int argc, char *argv[]) {
	boost::program_options::options_description options(
		"libfinder finds the libraries that define a given symbol.\nRun 'sudo updatedb' to make sure all libs are locatable, create an index with 'libfinder "
		"-u' (once every time your libs change) and look up a symbol with 'libfinder -s [symbol]' to get a list of libraries that define "
		"[symbol].\nParameters");
	int jobs = 0;
	const int hardware_concurrency = std::thread::hardware_concurrency();
	const auto update_description =
		"update lookup table (must be done before first use) with given number of threads (default=" + std::to_string(hardware_concurrency) + ")";
	options.add_options()                                                                                                        //
		("help,h", "print this")                                                                                                 //
		("update,u", boost::program_options::value<int>(&jobs)->implicit_value(hardware_concurrency), update_description.c_str()) //
		("symbol,s", boost::program_options::value<std::string>(), "the symbol to look up");//
		//("output-format,of", boost::program_options::value<std::string>()->default_value("symbol-list"), "Define the output format. Options are \tlist - pr");
	boost::program_options::variables_map variables_map;
	try {
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), variables_map);
	} catch (const boost::program_options::error &e) {
		std::cout << "error: " << e.what() << '\n' << "run '" << argv[0] << " --help' for details\n";
		return -1;
	}
	boost::program_options::notify(variables_map);
	if (variables_map.count("help")) {
		std::cout << options;
	}
	if (variables_map.count("update")) {
		if (jobs < 1) {
			std::cerr << "jobs must be at least 1\n";
			return -1;
		}
		update(jobs);
	}
	if (variables_map.count("symbol")) {
		const auto &prefix = variables_map["symbol"].as<std::string>();
		std::cout << "All symbols that have the prefix \"" << prefix << "\" and their libraries:\n";
		auto symbols = prefix_lookup(prefix);
		for (auto &symbol : symbols) {
			std::cout << symbol.get_symbol() << '\n';
			auto libs = symbol.get_libs_view();
			std::sort(std::begin(libs), std::end(libs));
			libs.erase(std::unique(std::begin(libs), std::end(libs)), std::end(libs));
			for (auto &lib : libs) {
				std::cout << '\t' << lib << '\n';
			}
		}
		return 0; //avoid double newline at end of output
	}
	std::cout << '\n';
}