#ifndef UTILITY_H
#define UTILITY_H

#include "main.h"

std::string get_output_from_command(string_view command);

template <class T, class U>
T any_cast(U *t) {
	return static_cast<T>(static_cast<void *>(t));
}

template <class T, class U>
T any_cast(const U *t) {
	return static_cast<T>(static_cast<const void *>(t));
}

#endif // UTILITY_H