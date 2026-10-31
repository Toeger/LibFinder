#include "iterable.h"
#include "symbol_database.h"

#include <iostream>

void iterable_tester() {
	Symbol_database::Reader reader;
	for (auto &lib : reader.libs("malloc")) {
		if (not lib.string().starts_with("/snap/")) {
			std::cout << lib << '\n';
		}
	}

	for (auto &[symbol, lib] : reader.symbol_libs("malloc")) {
		if (not lib.string().starts_with("/snap/")) {
			std::cout << symbol << ": " << lib << '\n';
		}
	}
}
