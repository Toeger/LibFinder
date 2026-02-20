#pragma once

#include <chrono>
#include <iostream>

namespace profile {
	inline auto now = std::chrono::high_resolution_clock::now();
	inline std::ostream *target = nullptr;
} // namespace profile

struct Endler {
	const Endler &operator<<(const auto &thing) const {
		if (profile::target) {
			(*profile::target) << ' ' << thing;
		}
		return *this;
	}
	~Endler() {
		if (profile::target) {
			(*profile::target) << std::endl;
		}
	}
};

#define PROF                                                                                                                                                   \
	(                                                                                                                                                          \
		[] {                                                                                                                                                   \
			if (profile::target == nullptr) {                                                                                                                  \
				return;                                                                                                                                        \
			}                                                                                                                                                  \
			auto n = std::chrono::high_resolution_clock::now();                                                                                                \
			(*profile::target) << __FILE__ << ':' << __LINE__ << ": "                                                                                          \
							   << static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(n - profile::now).count()) / 1000. << "s";         \
			profile::now = n;                                                                                                                                  \
		}(),                                                                                                                                                   \
		Endler{})
