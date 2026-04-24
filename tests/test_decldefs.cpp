#include "color.h"
#include "libclang.h"
#include "libparser.h"
#include "utility.h"

#include <catch2/catch_all.hpp>
#include <ranges>

TEST_CASE("Finding locations of mangled names") {
	const std::filesystem::path test_dir{TEST_DIR};
	const auto &command = "clang++-21";
	const auto &args = std::vector<std::string>{"-std=c++26", "-c", "test_data/decldefs.cpp", "-o", "test_data/decldefs.o"};
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
		const auto libs = parse_lib(test_dir / "test_data" / "decldefs.o", true, {});
		Libclang libclang{test_dir / "test_data" / "decldefs.cpp", {}};
		std::map<std::string_view /*mangled_name*/, Libclang::Symbol_Location> name_locations;
		name_locations.insert_range(libs | std::views::transform([](const Symbol &symbol) {
										return std::pair<const std::string_view, Libclang::Symbol_Location>{symbol.mangled_name, {}};
									}));
		libclang.get_locations(name_locations);
		for (auto &[mangled_name, locations] : name_locations) {
			INFO("While searching for " << (Symbol{{.type = Symbol_type::undefined, .mangled_name = std::string{mangled_name}}}));
			INFO(locations);
			CHECK(!!locations.declaration);
		}
	}
}
