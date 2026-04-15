#ifndef UTILITY_H
#define UTILITY_H

#include <filesystem>
#include <string>
#include <vector>

std::string get_output_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
std::string get_error_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
const std::string &get_install_status();

#endif // UTILITY_H
