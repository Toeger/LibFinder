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

struct Reverse_Lines : Iterable {
	std::string_view lines;
	char separator = '\n';

	std::optional<std::string_view> get_next() {
		const auto pos = lines.rfind(separator);
		if (pos == lines.npos) {
			if (not lines.empty()) {
				auto result = std::move(lines);
				lines = {};
				return result;
			}
			return std::nullopt;
		}
		std::string_view line = lines.substr(pos);
		lines.remove_suffix(line.size());
		line.remove_prefix(1);
		return line;
	}
};

struct Recently_Installed_Dpkg_Package {
	std::string_view package_name;
	std::string_view timestamp;
};

struct Recently_Installed_Dpkg_Packages : Iterable {
	Recently_Installed_Dpkg_Packages() {
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
			return number_string_compare(std::string_view{lhs.c_str()}, std::string_view{rhs.c_str()}) == std::strong_ordering::greater;
		});

		const auto &dpkg_log = dpkg_logs.back();
		packages = get_output_from_command(std::string_view{dpkg_log.c_str()}.ends_with(".gz") ? "zgrep" : "grep", {" status installed ", dpkg_log});
		package_input_stream = Reverse_Lines{.lines = packages};
	}

	void assign_next(std::optional<Recently_Installed_Dpkg_Package> &result) {
		if (auto log_entry = package_input_stream.get_next(); log_entry.has_value()) {
			auto &line = *log_entry;
			if (line.empty()) {
				return assign_next(result);
			}
			std::match_results<std::string_view::const_iterator> match;
			if (std::regex_match(std::begin(line), std::end(line), match, dpkg_entry)) {
				result = Recently_Installed_Dpkg_Package{
					.package_name = {match[2].first, match[2].second},
					.timestamp = {match[1].first, match[1].second},
				};
				return;
			} else {
				throw std::runtime_error{std::format("Failed parsing dpkg log in {} with line {}", dpkg_logs.back(), line)};
			}
		} else {
			dpkg_logs.pop_back();
			if (dpkg_logs.empty()) {
				result = std::nullopt;
				return;
			}
			packages = get_output_from_command(std::string_view{dpkg_logs.back().c_str()}.ends_with(".gz") ? "zgrep" : "grep",
											   {" status installed ", dpkg_logs.back()});
			package_input_stream = Reverse_Lines{{}, packages};
			return assign_next(result);
		}
	}

	std::vector<std::filesystem::path> dpkg_logs;
	std::string packages;
	Reverse_Lines package_input_stream;
	static const std::regex dpkg_entry;
};

const std::regex Recently_Installed_Dpkg_Packages::dpkg_entry{R"((\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) status installed ([^:]+):.*)"};

struct Dpkg_Packages : Iterable {
	void assign_next(std::optional<std::string_view> &result) {
		std::string_view res = view.subview(0, view.find("\n"));
		if (res.empty()) {
			result = std::nullopt;
			return;
		}
		view.remove_prefix(res.size() + 1);
		if (not res.starts_with("ii ")) {
			return assign_next(result);
		}
		res.remove_prefix(4);
		result = res.substr(0, res.find(' '));
	}

	std::string packages = get_output_from_command("dpkg", {"-l"});
	std::string_view view = packages;
};

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
