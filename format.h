#pragma once

#include <cstdint>
#include <string>
#include <string_view>

std::string bytes(std::size_t byte_count);
std::string number(std::uint64_t number);
std::string diff_highlight(std::string_view before, std::string text, std::string_view after);
