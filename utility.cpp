#if 0
#include "utility.h"
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
#else
#include "utility.h"

#include <memory>

static std::string get_output(const char *command, std::vector<std::string> argv, std::string postfix) {
	std::string com = command;
	for (auto &arg : argv) {
		com += " \"" + arg + "\"";
	}
	com += " " + postfix;
	std::unique_ptr<FILE, decltype([](FILE *f) { pclose(f); })> fp{popen(com.data(), "r")};
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
	return buffer;
}

std::string get_output_from_command(const char *command, std::vector<std::string> argv) {
	return get_output(command, argv, "2>/dev/null");
}

std::string get_error_from_command(const char *command, std::vector<std::string> argv) {
	return get_output(command, argv, "2>&1 1>/dev/null");
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

#endif
