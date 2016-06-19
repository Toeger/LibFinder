#include "test.h"
#include "generate.h"
#include <cassert>

bool test() {
	Map map;
	add_to_database(map, "/usr/lib/x86_64-linux-gnu/libboost_program_options.so");
	const auto symbol_to_find = "boost::program_options::arg";
	auto entry = map.lower_bound(symbol_to_find);
	assert(entry->first.find(symbol_to_find) == 0);
	return true;
}
