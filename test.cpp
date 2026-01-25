#include "test.h"
#include "argument_parser.h"
#include "asserts.h"
#include "libparser.h"
#include "reverse_prefix_tree.h"

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
	const auto &symbols = parse_lib("/usr/lib/x86_64-linux-gnu/libc.a", false);
	const auto symbol_to_find = "__pthread_create";
	for (const auto &symbol : symbols) {
		if (symbol.name == symbol_to_find) {
			return;
		}
	}
	assume(false);
}

static void test_reverse_prefix_tree() {
	Reverse_prefix_tree::Writer tree;
	std::array words = {"test", "tester", "tree", "tes"};
	std::array<std::size_t, std::size(words)> indexes;
	for (std::size_t i = 0; i < std::size(words); ++i) {
		indexes[i] = tree.add(words[i]);
		for (std::size_t j = 0; j < i; j++) {
			assume_equal(tree.test_read(indexes[j]), words[j]);
			assume_equal(indexes[j], tree.add(words[j]));
		}
	}

	std::filesystem::path path = "/tmp/reverse_prefix_tree";
	tree.write(path);

	Reverse_prefix_tree::Reader reader{path};
	for (std::size_t i = 0; i < std::size(words); ++i) {
		assume_equal(reader.get(indexes[i]), words[i]);
	}
}

bool test() {
	for (auto &function : {test_reading_arguments, test_symbol_loading, test_reverse_prefix_tree}) {
		function();
	}
	return true;
}
