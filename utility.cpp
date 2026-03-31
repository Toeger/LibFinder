#include "utility.h"
#include "libparser.h"

#include <cassert>
#include <format>
#include <iostream>
#include <memory>
#include <ranges>
#include <regex>

static std::string &replace_all(std::string &str, char old, std::string_view replacement) {
	for (std::size_t pos = str.find(old); pos != std::string::npos; pos = str.find(old, pos)) {
		str.replace(pos, 1, replacement);
		pos += std::size(replacement);
	}
	return str;
}

static std::string get_output(const char *command, std::vector<std::string> argv, std::string postfix, std::filesystem::path working_directory) {
	constexpr auto debug_output = false;
	std::string com;
	if (working_directory != ".") {
		com = "cd " + working_directory.string() + " && ";
	}
	com += command;
	for (auto &arg : argv) {
		com += " \"" + replace_all(arg, '"', R"(\")") + "\"";
	}
	com += " " + postfix;
	std::unique_ptr<FILE, decltype([](FILE *f) { pclose(f); })> fp{popen(com.data(), "r")};
	if constexpr (debug_output) {
		std::cerr << com << '\n';
	}
	if (!fp) {
		return {};
	}
	std::string buffer;
	const int buffersize = 1024;
	for (;;) {
		buffer.resize(buffer.size() + buffersize);
		std::size_t read = fread(&buffer[buffer.size() - buffersize], sizeof *buffer.data(), buffersize, fp.get());
		if (read < buffersize) {
			buffer.resize(buffer.size() - buffersize + read);
			break;
		}
	}
	if constexpr (debug_output) {
		std::cerr << buffer << '\n';
	}
	return buffer;
}

std::string get_output_from_command(const char *command, std::vector<std::string> argv, std::filesystem::path working_directory) {
	return get_output(command, argv, "2>/dev/null", working_directory);
}

std::string get_error_from_command(const char *command, std::vector<std::string> argv, std::filesystem::path working_directory) {
	return get_output(command, argv, "2>&1 1>/dev/null", working_directory);
}

const std::string &get_install_status() {
	static const std::string status = [] {
		std::string retval;
		auto dpkg_version = get_output_from_command("dpkg --robot --version", {});
		if (not dpkg_version.empty() and dpkg_version.front() == '1') {
			retval = get_output_from_command("ls -l /var/log/dpkg* | md5sum", {});
		}
		return retval;
	}();
	return status;
}

static std::string to_clang_query(std::string_view type_string, Symbol_location_type symbol_location_type) {
	if (type_string.ends_with('\n')) {
		type_string.remove_suffix(1);
	}
	auto name = type_string;
	Symbol::narrow_to_base_name(name);
	type_string.remove_prefix(std::size(name));

	int parenthesis_count = 0;
	int template_count = 0;
	int param_count = 0;
	bool is_function = false;

	for (std::size_t i = 0; i < std::size(type_string); i++) {
		switch (type_string[i]) {
			case '(':
				parenthesis_count++;
				if (template_count == 0) {
					is_function = true;
				}
				continue;
			case ')':
				parenthesis_count--;
				if (parenthesis_count == 0 and template_count == 0 and type_string[i - 1] != '(') {
					param_count++;
				}
				continue;
			case '<':
				template_count++;
				continue;
			case '>':
				template_count--;
				continue;
			case ',':
				if (parenthesis_count == 1 and template_count == 0) {
					param_count++;
				}
				continue;
		}
	}
	const char *decldef{};
	switch (symbol_location_type) {
		case Symbol_location_type::declarations:
			decldef = R"(
	unless(
		isDefinition()
	),)";
			break;
		case Symbol_location_type::definitions:
			decldef = R"(
	isDefinition(),)";
			break;
		case Symbol_location_type::declarations_and_definitions:
			decldef = "";
	}
	return std::format(R"(match {}Decl({}
	hasName('::{}'){}
))",
					   is_function ? "function" : "var", decldef, name, is_function ? std::format(",\n\tparameterCountIs({})", param_count) : "");
}

std::vector<std::pair<std::filesystem::path, int>> get_locations(std::string_view type_string, std::filesystem::path file,
																 std::filesystem::path compile_commands_json_directory,
																 Symbol_location_type symbol_location_type) {
	const auto &query = to_clang_query(type_string, symbol_location_type);
	const auto result = get_output_from_command("clang-query-21", {"-c", "set output detailed-ast", "-c", query, "-p", compile_commands_json_directory, file});
	std::vector<std::pair<std::filesystem::path, int>> retval;
	bool match_regex = false;
	for (auto line_match : std::ranges::views::split(result, '\n')) {
		std::string_view line{line_match};
		if (match_regex) {
			match_regex = false;
			static const std::regex regex{R"(.*Decl 0x[0-9a-f]+ <([^:]+):(\d+).*)", std::regex::optimize};
			std::cmatch match;
			if (std::regex_match(std::begin(line), std::end(line), match, regex)) {
				const auto &matched_file = match[1];
				const auto &matched_line = match[2];
				retval.push_back({std::filesystem::weakly_canonical({matched_file}), std::stoi(matched_line)});
			} else {
				throw std::runtime_error{std::format("Failed parsing file and line number from {}", line)};
			}
		} else if (line == "Binding for \"root\":") {
			match_regex = true;
		}
	}
	return retval;
}

namespace {
#if 0
#include "raii.h"

#include <cassert>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

std::string get_output_from_command(const char *command, std::vector<std::string> argv) {
	constexpr std::size_t read = 0;
	constexpr std::size_t write = 1;
	int child_outfds[2]{-1, -1};

	static constexpr auto closefd = [](int &fd) {
		if (fd != -1) {
			if (close(fd)) {
				throw std::runtime_error{std::format("Failed close, error code {}", errno)};
			}
			fd = -1;
		}
	};

	RAII _{[&child_outfds] {
		closefd(child_outfds[read]);
		closefd(child_outfds[write]);
	}};

	argv.insert(std::begin(argv), command);
	std::vector<char *> args(std::size(argv) + 1);
	for (std::size_t i = 0; i < std::size(argv); i++) {
		args[i] = argv[i].data();
	}
	args.back() = nullptr;

	if (pipe(child_outfds) != 0) {
		throw std::runtime_error{std::format("Failed pipe, error code {}", errno)};
	}

	if (int pid = fork()) { //parent
		if (pid == -1) {
			throw std::runtime_error{std::format(R"(Failed fork, error code {})", errno)};
		}
		RAII _{[&pid] {
			//while (waitpid(-1, nullptr, WNOHANG) > 0)
			//	;
			waitpid(pid, nullptr, 0);
		}};

		closefd(child_outfds[write]);

		constexpr int buffersize = 1024 * 128;
		std::string buffer;
		char buf[buffersize];
		for (;;) {
			ssize_t bytes_obtained = ::read(child_outfds[read], buf, buffersize);
			if (bytes_obtained <= 0) {
				//std::println(stderr, "{} read error {} from {}, error code {}", fd == out[read] ? "stdout" : "stderr", bytes_obtained, command, errno);
				return buffer;
			}
			buffer.insert(std::end(buffer), buf, buf + bytes_obtained);
		}
	} else { //child
		int dummy_fds[2];
		if (pipe(dummy_fds) != 0) {
			throw std::runtime_error{std::format("Failed pipe, error code {}", errno)};
		}
		static constexpr auto close_fd = [](int fd) {
			if (close(fd)) {
				std::cerr << "close(" << fd << ") failed with " << errno << '\n';
				std::exit(-1);
			}
		};
		static constexpr auto dup2_fd = [](int old_fd, int new_fd) {
			if (dup2(old_fd, new_fd) == -1) {
				std::cerr << "dup2(" << old_fd << ", " << new_fd << ") failed with " << errno << '\n';
				std::exit(-1);
			}
		};
		dup2_fd(dummy_fds[read], STDIN_FILENO);
		close_fd(dummy_fds[write]);
		close_fd(child_outfds[read]);
		dup2_fd(child_outfds[write], STDOUT_FILENO);
		dup2_fd(child_outfds[write], STDERR_FILENO);
		execvp(command, args.data());
		std::println(stderr, "Failed to call execvp with {} because of error code {}", command, errno);
		std::exit(-1);
	}
}
#endif
} // namespace
