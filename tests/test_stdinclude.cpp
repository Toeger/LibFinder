#include "color.h"
#include "generate.h"
#include "libclang.h"
#include "libparser.h"
#include "symbol_database.h"
#include "utility.h"

#include <catch2/catch_all.hpp>
#include <iostream>

TEST_CASE("Including iostream") {
	const std::filesystem::path test_dir{TEST_DIR};
	const auto &command = "clang++-21";
	const auto &source = "test_data/stdinclude.cpp";
	const auto &compiled = "test_data/stdinclude.o";
	const auto &args = std::vector<std::string>{"-std=c++26", "-c", source, "-o", compiled};
	auto [compile_result, output] = run_command(command, args, test_dir);
	{
		INFO("Compiling \"" << command << [&args] {
			std::string result;
			for (auto &arg : args) {
				result += ' ';
				result += arg;
			}
			return result;
		}() << '"');
		INFO(output);
		REQUIRE(compile_result == 0);
	}
	{
		INFO("Inspecting " << Color::file(std::filesystem::absolute(test_dir / source).string()));
		INFO("Compiled " << Color::file(std::filesystem::absolute(test_dir / compiled).string()));
		Symbol_database::Reader reader{data_base_filepath};

		const auto libs = parse_lib(test_dir / "test_data" / "stdinclude.o", true, {});
		Libclang libclang{test_dir / "test_data" / "stdinclude.cpp", {}};
		std::map<std::string_view /*mangled_name*/, Libclang::Symbol_Location> name_locations;
		name_locations.insert_range(libs | std::views::transform([](const Symbol &symbol) {
										return std::pair<const std::string_view, Libclang::Symbol_Location>{symbol.mangled_name, {}};
									}));
		libclang.get_locations(name_locations);
		for (auto &[mangled_name, locations] : name_locations) {
			if (not locations.declaration) {
				INFO("Failed finding " << Symbol{{.mangled_name = std::string{mangled_name}}});
				INFO(locations);
				CHECK(not reader.libraries_from_symbol(mangled_name).empty());
				continue;
			}
			INFO(locations);
			CHECK(locations.declaration);
		}
	}
}
