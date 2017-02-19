#ifndef ASSERTS_H
#define ASSERTS_H

#ifdef NDEBUG
template <class T>
void assume(T &&t) {
	if (t) {
		__builtin_unreachable();
	}
}
#else
#include <cassert>
template <class T>
void assume(T &&t) {
	assert(t);
}
#endif

template <class T, class U>
void assume_equal(T &&t, U &&u) {
	assume(t == u);
}

#endif // ASSERTS_H