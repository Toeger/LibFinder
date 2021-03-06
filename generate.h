#ifndef GENERATE_H
#define GENERATE_H

#include "main.h"

#include <map>
#include <string>

using Map = std::map<std::string, std::string>;

int add_to_database_so(Map &symbol_file_map, const string_view file_path);
int add_to_database_a(Map &symbol_file_map, const string_view file_path);
void update(int jobs);

#endif // GENERATE_H