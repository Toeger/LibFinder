#pragma once

#include <chrono>
#include <iostream>

namespace profile {
	inline auto now = std::chrono::high_resolution_clock::now();
}

struct Endler {
	std::ostream &operator<<(const auto &thing) const {
		return os << thing << ' ';
	}
	~Endler() {
		os << std::endl;
	}
	std::ostream &os;
};

#define PROF                                                                                                                                                   \
	Endler{std::cerr} << [] {                                                                                                                                  \
		auto n = std::chrono::high_resolution_clock::now();                                                                                                    \
		std::cerr << __FILE__ << ':' << __LINE__ << ": "                                                                                                       \
				  << static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(n - profile::now).count()) / 1000. << "s";                      \
		profile::now = n;                                                                                                                                      \
		return "";                                                                                                                                             \
	}()
