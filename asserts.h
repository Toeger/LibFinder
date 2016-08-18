#ifndef ASSERTS_H
#define ASSERTS_H

#include <cassert>

template <class T, class U>
void assert_equal(T &&t, U &&u){
	assert(t == u);
}

#endif // ASSERTS_H