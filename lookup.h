#ifndef LOOKUP_H
#define LOOKUP_H

#include "main.h"

#include <string>
#include <vector>

std::vector<std::string> lookup(string_view symbol, Search_type st);

#endif // LOOKUP_H