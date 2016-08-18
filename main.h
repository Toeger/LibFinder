#ifndef MAIN_H
#define MAIN_H

#include <experimental/string_view>
#include <string>

using string_view = std::experimental::string_view;

static const auto entry_separator = '\0'; //must be illegal in symbol names and paths
static const auto file_separator = '\1';  //must be illegal in paths

extern const std::string data_base_path;
extern const std::string data_base_filepath;
extern const std::string data_base_index_filepath;
extern const std::string symbolic_links_filepath;
extern const std::string symbolic_links_index_filepath;

#endif // MAIN_H
