#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::string> get_file_path(
    const std::string& directory_path = "./dataset");