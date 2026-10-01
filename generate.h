#pragma once

#include <filesystem>
#include <thread>
#include <vector>

void update(unsigned int jobs = std::thread::hardware_concurrency());
void update(std::vector<std::filesystem::path> libraries, unsigned int jobs = std::thread::hardware_concurrency());
