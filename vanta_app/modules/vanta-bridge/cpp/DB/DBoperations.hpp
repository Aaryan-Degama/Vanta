#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"
#include <cstdint>

struct file_meta
{
    int64_t id;
    std::string content_uri;
    std::string display_name;
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

std::string get_database_stats_json(
    const std::string& db_path
);

// ── Documents (full-text search over extracted text) ──

// Up to `limit` pending documents as a JSON array of {id, path, display_name}.
std::string get_pending_documents_json(const std::string& db_path, int limit = 50);

// Replaces the document's full-text row and marks the file indexed.
bool save_document_text(const std::string& db_path, int64_t file_id,
                        const std::string& name, const std::string& body);

// Turns free text into a safe FTS5 query: every word a quoted prefix term, ANDed.
std::string fts_query_from_words(const std::string& raw);

// Best-matching documents as a JSON array (file_id, abs_path, display_name,
// size_bytes, mtime_unix, snippet).
std::string search_documents_json(const std::string& db_path, const std::string& query, int limit = 30);
