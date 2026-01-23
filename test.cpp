#include "test.h"
#include "argument_parser.h"
#include "asserts.h"
#include "libparser.h"
#include "test_radix_tree.h"

static void test_reading_arguments() {
	char short_help[] = "-h";
	char long_help[] = "--help";
	char short_update[] = "-u";
	char long_update[] = "--update";
	std::pair<std::vector<std::string_view>, Argument_parser::Argument_type> inputs[] = {
		{{}, Argument_parser::Argument_type::invalid},			 {{short_help}, Argument_parser::Argument_type::help},
		{{long_help}, Argument_parser::Argument_type::help},	 {{short_update}, Argument_parser::Argument_type::update},
		{{long_update}, Argument_parser::Argument_type::update},
	};
	for (auto &input : inputs) {
		assume_equal(Argument_parser::get_argument_type(input.first), input.second);
	}
}

static void test_symbol_loading() {
	const auto &symbols = parse_lib("/usr/lib/x86_64-linux-gnu/libboost_program_options.so", true);
	const auto symbol_to_find = "boost::program_options::arg";
	for (const auto &symbol : symbols) {
		if (symbol.demangled_name() == symbol_to_find) {
			return;
		}
	}
	assume(false);
}

bool test() {
	for (auto &function : {test_reading_arguments, test_symbol_loading, test_radix_tree}) {
		function();
	}
	return true;
}
