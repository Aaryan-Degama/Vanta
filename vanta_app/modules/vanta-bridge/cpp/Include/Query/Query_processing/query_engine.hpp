#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"

struct search_result {
    int64_t file_id;
    std::string abs_path;
    std::string display_name;
    int64_t size_bytes;
    int64_t mtime_unix;
    float distance;
};

#include "CLIP_model.hpp"

// Initializes the query engine, loading any necessary text models.
bool init_query_engine();

// Given a list of integer tokens and database path, returns the top K similar images.
std::vector<search_result> search_images(const std::string& db_path, const std::vector<int64_t>& tokens, CLIP_session* clip_session, int top_k = 30);
