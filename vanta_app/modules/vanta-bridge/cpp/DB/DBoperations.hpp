#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"
#include <cstdint>

struct file_meta
{
    std::string content_uri;
    std::string file_type;
    std::string mime_type;

    int64_t size_bytes;
    int64_t mtime_unix;
    int64_t last_indexed_at;

    int width_px;
    int height_px;
    int64_t duration_ms;

    std::string status;
    int retry_count;
};

sqlite3* initialize_database(
    const std::string& db_path
);

bool insert_file(
    sqlite3* db,
    const file_meta& file
);

bool insert_files(
    const std::vector<file_meta>& files,
    const std::string& db_path
);

std::vector<file_meta> get_files(
    const std::string& db_path,
    int limit = 100
);

int get_file_count(
    const std::string& db_path
);