#ifndef UTILITY_H
#define UTILITY_H

#include <string>
#include <string_view>

struct Output {
	std::string output;
	std::string error;
};

Output get_output_from_command(std::string_view command);

#endif // UTILITY_H
