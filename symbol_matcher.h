#pragma once

#include "libparser.h"
#include "symbol_database.h"

#include <stdexcept>

struct Symbol_matcher {
	Symbol_matcher(std::filesystem::path data_base);

	struct Type_and_origin_index {
		Symbol_type type;
		std::size_t origin_index;
	};

	void add(Symbol symbol, std::filesystem::path origin);

	struct Unresolved_result : std::runtime_error {
		struct Unresolved_result_aggregate {
			Symbol symbol;
			std::filesystem::path origin;
		};
		Unresolved_result(Unresolved_result_aggregate unresolved)
			: std::runtime_error{std::format("Error: Unresolved symbol {} required by {}", unresolved.symbol.demangled_name(), unresolved.origin.c_str())}
			, symbol{std::move(unresolved.symbol)}
			, origin{std::move(unresolved.origin)} {}

		Symbol symbol;
		std::filesystem::path origin;
	};

	struct Duplicate_symbol_error : std::runtime_error {
		struct Duplicate_symbol_error_aggregate {
			Symbol symbol;
			std::filesystem::path lib1, lib2;
		};

		Duplicate_symbol_error(Duplicate_symbol_error_aggregate dsea)
			: std::runtime_error{std::format("Error: Duplicate symbol definition for {} in\n{} and\n{}", dsea.symbol.demangled_name(), dsea.lib1.c_str(),
											 dsea.lib2.c_str())}
			, symbol{std::move(dsea.symbol)}
			, lib1{std::move(dsea.lib1)}
			, lib2{std::move(dsea.lib2)} {}
		Symbol symbol;
		std::filesystem::path lib1, lib2;
	};

	void load_compile_commands_json(std::filesystem::path file_path);

	std::string resolve_to_command();

	void add_lib(std::filesystem::path lib);

	bool is_resolved() const;

	std::size_t origin_index(std::filesystem::path origin);

	std::map<std::string /*symbol*/, Type_and_origin_index> defined, undefined;
	std::vector<std::filesystem::path /*libs*/> origins;
	Symbol_database::Reader db;
	std::map<std::filesystem::path /*input file*/, std::vector<std::filesystem::path> /*priorities*/> path_priorities;
};
