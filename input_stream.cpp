#include "input_stream.h"
#include "symbol_database.h"

#include <iostream>

void input_stream_tester() {
	//static_assert(detail::Input_Stream::is_input_stream_producer<&Symbol_database::Reader::libraries_of_symbol>);
	//static_assert(not detail::Input_Stream::is_member_input_stream_producer<&Symbol_database::Reader::libraries_of_symbol>);

	//static_assert(
	//	std::is_same_v<detail::Input_Stream::input_stream_producer_product_type<&Symbol_database::Reader::libraries_of_symbol>, std::filesystem::path>);

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
