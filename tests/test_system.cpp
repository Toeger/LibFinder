#include "system.h"
#include "utility.h"

#include <catch2/catch_all.hpp>
#include <iostream>

TEST_CASE("Get dpkg packages", "system") {
	for (auto &package_sv : Dpkg_Packages{} | iterable | limit(3) | [](std::string_view sv) { return std::string{sv}; }) {
		std::string package{package_sv};
		const auto &status = get_output_from_command("dpkg", {"--status", "--robot", package});
		if (auto pos = package.find(':'); pos != package.npos) {
			package.resize(pos);
		}
		INFO("Package: " << package << "\nOutput: " << status);
		REQUIRE(status.contains("Package: " + package));
		//std::cout << package << std::endl;
	}
}

#if 0
void test_system() {
	profile::target = &std::cerr;
	static const std::regex lib_file{R"(.*/lib[^/]*\.(a|so)$)"};
	std::vector<std::string> dpkg_command{"-L"};
	dpkg_command.append_range(Dpkg_Packages{} | [](std::string_view sv) { return std::string{sv}; });
	PROF << "to get list of packages";
	
	std::vector<std::filesystem::path> libfiles;
	const auto package_list = get_output_from_command("dpkg", std::move(dpkg_command));
	PROF << "to get list of files from dpkg";
	for (auto file : package_list | std::views::split(std::string_view{"\n"})) {
		std::string_view line{file};
		if (not(line.ends_with(".so") or line.ends_with(".a"))) {
			continue;
		}
		const auto slash_pos = line.rfind('/');
		line.remove_prefix(slash_pos + 1);
		if (line.starts_with("lib")) {
			libfiles.push_back(line);
		}
	}
	PROF << "to parse list of" << std::size(libfiles) << "lib files";
	
	update(libfiles);
	
	PROF << "to parse symbols";
}
#endif
