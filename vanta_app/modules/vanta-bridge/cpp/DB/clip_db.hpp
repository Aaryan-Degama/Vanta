#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"

#include "DBoperations.hpp"

// Initialize the clip_vec vector DB
bool init_clip_db(sqlite3* db);

// Fetch unindexed pictures
std::vector<file_meta> get_unindexed_images(
    const std::string& db_path,
    int limit = 100
);

// Get total count of pictures for progress
int get_picture_count(const std::string& db_path);

// Get count of remaining unindexed pictures
int get_unindexed_picture_count(const std::string& db_path);

// Save the 512-dim embedding to clip_vec and mark the file as indexed
bool save_clip_embedding(
    sqlite3* db,
    int64_t file_id,
    const std::vector<float>& embedding
);

// Update status of a file (e.g., 'failed')
bool update_file_status(
    sqlite3* db,
    int64_t file_id,
    const std::string& status
);
