#pragma once

#include <utility>

template <class Init_function, class Exit_function>
struct RAII {
	RAII(Init_function init_function, Exit_function p_exit_function)
		: exit_function{std::move(p_exit_function)} {
		init_function();
	}
	RAII(Exit_function p_exit_function)
		: RAII([] {}, std::move(p_exit_function)) {}
	~RAII() {
		if (not canceled) {
			exit_function();
		}
	}
	void cancel() {
		canceled = true;
	}
	void early_exit() {
		if (not canceled) {
			exit_function();
			cancel();
		}
	}

	private:
	Exit_function exit_function;
	bool canceled = false;
};

template <class Init_function, class Exit_function>
RAII(Init_function, Exit_function) -> RAII<Init_function, Exit_function>;
template <class Exit_function>
RAII(Exit_function) -> RAII<void(), Exit_function>;
