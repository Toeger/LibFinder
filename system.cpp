#include "system.h"
#include "cast.h"
#include "generate.h"
#include "profile.h"
#include "symbol_database.h"
#include "utility.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <regex>

constexpr bool is_number(char c) {
	return c >= '0' and c <= '9';
}

constexpr std::strong_ordering number_compare(std::string_view &lhs, std::string_view &rhs) {
	const std::string_view lhsnum{std::begin(lhs), std::find_if_not(std::begin(lhs), std::end(lhs), is_number)};
	const std::string_view rhsnum{std::begin(rhs), std::find_if_not(std::begin(rhs), std::end(rhs), is_number)};
	lhs.remove_prefix(lhsnum.size());
	rhs.remove_prefix(rhsnum.size());
	if (const auto size_comp = lhsnum.size() <=> rhsnum.size(); size_comp != std::strong_ordering::equal) {
		return size_comp;
	}
	return lhsnum <=> rhsnum;
}

constexpr std::strong_ordering number_string_compare(std::string_view lhs, std::string_view rhs) {
	for (;;) {
		if (lhs.empty()) {
			return rhs.empty() ? std::strong_ordering::equal : std::strong_ordering::less;
		}
		if (rhs.empty()) {
			return std::strong_ordering::greater;
		}
		const char clhs = lhs.front();
		const char crhs = rhs.front();
		if (clhs == crhs) {
			lhs.remove_prefix(1);
			rhs.remove_prefix(1);
			continue;
		}
		if (is_number(clhs)) {
			if (is_number(crhs)) {
				if (const auto result = number_compare(lhs, rhs); result == std::strong_ordering::less) {
					return result;
				} else if (result == std::strong_ordering::greater) {
					return result;
				} else {
					continue;
				}
			} else {
				return std::strong_ordering::greater;
			}
		} else {
			if (is_number(crhs)) {
				return std::strong_ordering::less;
			} else {
				return clhs <=> crhs;
			}
		}
	}
}

void reverse_lines(std::string_view data, std::invocable<std::string_view> auto &&callback, const char separator = '\n') {
	for (;;) {
		const auto pos = data.rfind(separator);
		if (pos == data.npos) {
			if (not data.empty()) {
				callback(std::move(data));
			}
			return;
		}
		std::string_view line = data.substr(pos);
		data.remove_suffix(line.size());
		line.remove_prefix(1);
		if (line.empty()) {
			continue;
		}
		callback(std::move(line));
	}
}

struct Recently_Installed_Dpkg_Package {
	std::string_view package_name;
	std::string_view timestamp;
};

void recently_installed_dpkg_packages(std::invocable<Recently_Installed_Dpkg_Package> auto &&callback) {
	std::vector<std::filesystem::path> dpkg_logs;
	for (const auto &dir : std::filesystem::directory_iterator{"/var/log/"}) {
		if (not std::string_view{dir.path().filename().c_str()}.starts_with("dpkg.")) {
			continue;
		}
		if (dir.is_directory()) {
			continue;
		}
		dpkg_logs.push_back(dir);
	}
	std::ranges::sort(dpkg_logs, [](const std::filesystem::path &lhs, const std::filesystem::path &rhs) {
		//throwing away the strong ordering makes me cry
		return number_string_compare(std::string_view{lhs.c_str()}, std::string_view{rhs.c_str()}) == std::strong_ordering::less;
	});

	std::regex dpkg_entry{R"((\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) status installed ([^:]+):.*)"};
	for (auto &dpkg_log : dpkg_logs) {
		const auto packages = get_output_from_command(std::string_view{dpkg_log.c_str()}.ends_with(".gz") ? "zgrep" : "grep", {" status installed ", dpkg_log});
		reverse_lines(packages, [&](std::string_view line) {
			std::match_results<std::string_view::const_iterator> match;
			if (std::regex_match(std::begin(line), std::end(line), match, dpkg_entry)) {
				callback(Recently_Installed_Dpkg_Package{
					.package_name = {match[2].first, match[2].second},
					.timestamp = {match[1].first, match[1].second},
				});
			} else {
				throw std::runtime_error{std::format("Failed parsing dpkg log in {} with line {}", dpkg_log, line)};
			}
		});
	}
}

void dpkg_packages(std::invocable<std::string_view> auto &&callback) {
	for (auto range : get_output_from_command("dpkg", {"-l"}) | std::views::split(std::string_view{"\n"})) {
		std::string_view line{range};
		if (not line.starts_with("ii ")) {
			continue;
		}
		line.remove_prefix(4);
		callback(line.substr(0, line.find(' ')));
	}
}

void test_system() {
	profile::target = &std::cerr;
	static const std::regex lib_file{R"(.*/lib[^/]*\.(a|so)$)"};
	std::vector<std::string> dpkg_command{"-L"};
	dpkg_packages([&dpkg_command](std::string_view package) { dpkg_command.push_back(std::string{package}); });
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

const std::string &get_install_status() {
	static const std::string status = [] {
		std::string retval;
		auto dpkg_version = get_output_from_command("dpkg --robot --version", {});
		if (not dpkg_version.empty() and dpkg_version.front() == '1') {
			retval = get_output_from_command("ls -l /var/log/dpkg* | md5sum", {});
		}
		return retval + "/" __DATE__ "/" __TIME__;
	}();
	return status;
}
