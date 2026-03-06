#ifndef UTILITY_H
#define UTILITY_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

std::string get_output_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
std::string get_error_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
const std::string &get_install_status();
std::vector<std::pair<std::filesystem::path, int /*line*/>> get_locations(std::string_view type_string, std::filesystem::path file,
																		  std::filesystem::path compile_commands_json_directory);

#endif // UTILITY_H
