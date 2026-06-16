#include "getfiles.hpp"

#include <unordered_set>
#include <algorithm>
#include <cctype>

std::vector<std::string> get_file_path(
    const std::string &directory_path) {

    std::unordered_set<std::string> imageExtensions = {
        ".jpg", ".jpeg",
        ".png",
        ".gif",
        ".bmp",
        ".webp",
        ".tiff", ".tif",
        ".svg",
        ".ico",
        ".heic", ".heif",
        ".avif"
    };

    std::vector<std::string> paths;
    for (const auto& entry : fs::recursive_directory_iterator(directory_path)){
        if (!entry.is_regular_file())
            continue;

        std::string ext = entry.path().extension().string();

        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (imageExtensions.contains(ext)) {
            paths.push_back(entry.path().string());
        }
    }

    return paths;
}
    