#ifndef UTILITY_H
#define UTILITY_H

#include <string>
#include <vector>

std::string get_output_from_command(const char *command, std::vector<std::string> argv);
std::string get_error_from_command(const char *command, std::vector<std::string> argv);

#endif // UTILITY_H
