#include "system.h"
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

template <class F, class... Args>
concept Function = requires(F &&f) {
	{ f(std::declval<Args>()...) } -> std::convertible_to<bool>;
} or requires(F &&f) {
	{ f(std::declval<Args>()...) } -> std::same_as<void>;
};

template <class... Args>
bool bool_call(Function<Args...> auto &&callback, Args... args) {
	if constexpr (std::is_void_v<decltype(callback(std::forward<Args>(args)...))>) {
		callback(std::forward<Args>(args)...);
		return true;
	} else {
		return callback(std::forward<Args>(args)...);
	}
}

void reverse_lines(std::string_view data, Function<std::string_view> auto &&callback) {
	for (;;) {
		const auto pos = data.rfind('\n');
		if (pos == data.npos) {
			if (not data.empty()) {
				callback(data);
			}
			return;
		}
		std::string_view line = data.substr(pos);
		data.remove_suffix(line.size());
		line.remove_prefix(1);
		if (line.empty()) {
			continue;
		}
		if (not bool_call(callback, line)) {
			return;
		}
	}
}

struct Recently_Installed_Dpkg_Package {
	std::string_view package_name;
	std::string_view timestamp;
};

void recently_installed_dpkg_packages(Function<Recently_Installed_Dpkg_Package> auto &&callback) {
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
				if (not bool_call<Recently_Installed_Dpkg_Package>(callback, {
																				 .package_name = std::string_view{match[2].first, match[2].second},
																				 .timestamp = std::string_view{match[1].first, match[1].second},
																			 })) {
					return;
				}
			} else {
				throw std::runtime_error{std::format("Failed parsing dpkg log in {} with line {}", dpkg_log, line)};
			}
		});
	}
}

static void get_last_apt_install() {
	recently_installed_dpkg_packages([](Recently_Installed_Dpkg_Package &&package) { std::cout << package.package_name << " " << package.timestamp << '\n'; });
}

void test_system() {
	get_last_apt_install();
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
