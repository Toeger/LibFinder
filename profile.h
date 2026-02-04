#pragma once

#include <chrono>

namespace profile {
	inline auto now = std::chrono::high_resolution_clock::now();
}
#define PROF                                                                                                                                                   \
	do {                                                                                                                                                       \
		auto n = std::chrono::high_resolution_clock::now();                                                                                                    \
		std::cerr << __FILE__ << ':' << __LINE__ << ": "                                                                                                       \
				  << static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(n - profile::now).count()) / 1000. << "s\n";                    \
		profile::now = n;                                                                                                                                      \
	} while (0)
