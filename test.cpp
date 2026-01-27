#include "test.h"
#include "asserts.h"
#include "libparser.h"
#include "symbol_database.h"
#include <set>

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

static void test_symbol_database() {
	const std::vector<std::vector<std::pair<std::string, std::string>>> test_cases{
		{{"symbol", "/first library"}},
		{{"symbol", "/first library"}, {"other symbol", "/second library"}},
		{{"symbol", "/first library"}, {"symbol", "/second library"}},
		{{"symbol", "/first library"}, {"other symbol", "/first library"}},
	};
	auto libs = [](const std::vector<std::pair<std::string, std::string>> &test_case, std::string_view symbol) {
		std::set<std::string_view> result;
		for (auto &[result_symbol, lib] : test_case) {
			if (result_symbol == symbol) {
				result.insert(lib);
			}
		}
		return result;
	};
	const auto &path = "/tmp/symbol_database";
	for (auto &test_case : test_cases) {
		{
			Symbol_database::Writer db;
			for (auto &[symbol, library] : test_case) {
				db.add(symbol, library);
			}
			db.write(path);
		}
		{
			Symbol_database::Reader db{path};
			for (auto &[symbol, library] : test_case) {
				auto result_libs = db.libraries_from_symbol(symbol);
				auto test_libs = libs(test_case, symbol);
				assume_equal(result_libs.size(), test_libs.size());
				for (auto &result : result_libs) {
					assume(test_libs.contains(result));
				}
			}
		}
	}
}

bool test() {
	for (auto &function : {test_symbol_loading, test_symbol_database}) {
		function();
	}
	return true;
}
