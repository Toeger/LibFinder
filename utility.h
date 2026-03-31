#ifndef UTILITY_H
#define UTILITY_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

std::string get_output_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
std::string get_error_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
const std::string &get_install_status();
enum class Symbol_location_type { declarations = 1, definitions, declarations_and_definitions };
std::vector<std::pair<std::filesystem::path, int /*line*/>> get_locations(std::string_view type_string, std::filesystem::path file,
																		  std::filesystem::path compile_commands_json_directory,
																		  Symbol_location_type symbol_location_type);

#endif // UTILITY_H
