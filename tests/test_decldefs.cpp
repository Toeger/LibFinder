#include "color.h"
#include "libclang.h"
#include "libparser.h"
#include "utility.h"

#include <catch2/catch_all.hpp>
#include <iostream>

TEST_CASE("Finding locations of mangled names") {
	const std::filesystem::path test_dir{TEST_DIR};
	auto [compile_result, output] = run_command("c++", {"-std=c++26", "-c", "test_data/decldefs.cpp", "-o", "test_data/decldefs.o"}, test_dir);
	{
		INFO(output);
		REQUIRE(compile_result == 0);
	}
	{
		const auto libs = parse_lib(test_dir / "test_data" / "decldefs.o", true, {});
		Libclang libclang{test_dir / "test_data" / "decldefs.cpp", {}};
		for (auto &lib : libs) {
			//std::cout << lib.mangled_name << '(' << lib.demangled_name() << ")\n";
			const auto &locations = libclang.get_locations(lib.mangled_name);
			INFO((locations.is_definition ? "Definition" : "Declaration")
				 << " of " << Color::symbol(lib.mangled_name) << '(' << Color::symbol(lib.demangled_name()) << ')' << " at ");
			REQUIRE(locations.declaration);
			//if (locations.declaration) {
			//	std::cout << Color::file(locations.declaration.path.string()) << ':' << Color::line(locations.declaration.line) << '\n';
			//} else {
			//	std::cout << Color::red("unknown") << '\n';
			//}
		}
	}
}
