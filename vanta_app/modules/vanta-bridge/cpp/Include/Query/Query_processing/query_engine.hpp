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
#include "typo_rectifyier.hpp"

// Initializes the query engine, loading any necessary text models.
bool init_query_engine();

// Corrects the query using the typo rectifier
std::string get_corrected_query(const std::string& raw_query);

// Forward declaration for tokenizer
class CLIPTokenizer;

// Given a raw string query and database path, returns the top K similar images.
std::vector<search_result> search_images(const std::string& db_path, const std::string& query, CLIP_session* clip_session, CLIPTokenizer* tokenizer, int top_k = 30);
