#ifndef MAIN_H
#define MAIN_H

#include <experimental/string_view>
#include <map>
#include <string>

using Map = std::map<std::string, std::string>;

using string_view = std::experimental::string_view;

static const auto entry_separator = '\0'; //must be illegal in symbol names and paths
static const auto file_separator = '\1';  //must be illegal in paths

enum class Search_type { exact, prefix };

extern const std::string data_base_file;

#endif // MAIN_H
