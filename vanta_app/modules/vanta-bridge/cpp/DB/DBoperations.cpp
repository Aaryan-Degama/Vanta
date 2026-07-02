#include "DBoperations.hpp"
#include <iostream>
#include <android/log.h>
#define SQLITE_CORE 1
#include "sqlite-vec.h"
#include <android/log.h>

#define LOG_TAG "VantaDB"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool init_graph_schema(sqlite3* db);
bool init_face_schema(sqlite3* db);

sqlite3* initialize_database(
    const std::string& db_path)
{
    sqlite3* db = nullptr;

    const std::string DB_SCHEMA = R"(
CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY,
    abs_path        TEXT    NOT NULL UNIQUE,
    display_name    TEXT    NOT NULL DEFAULT '',
    filetype        TEXT    NOT NULL CHECK(filetype IN ('picture','document','audio','video')),
    mime_type       TEXT,
    size_bytes      INTEGER,
    mtime_unix      INTEGER NOT NULL,
    last_indexed_at INTEGER,
    width_px        INTEGER,
    height_px       INTEGER,
    duration_ms     INTEGER,
    face_count      INTEGER NOT NULL DEFAULT 0,  -- total detected faces, used for exclusive entity queries
    status          TEXT    NOT NULL DEFAULT 'pending'
                    CHECK(status IN ('pending','indexed','failed','skipped')),
    retry_count     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_files_filetype ON files(filetype);
CREATE INDEX IF NOT EXISTS idx_files_mtime    ON files(mtime_unix);
CREATE INDEX IF NOT EXISTS idx_files_status     ON files(status);
CREATE INDEX IF NOT EXISTS idx_files_face_count ON files(face_count);
)";

    sqlite3_auto_extension((void (*)())sqlite3_vec_init);

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        LOGE("Failed to open DB: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return nullptr;
    }

    char* err_msg = nullptr;
    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;", nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        LOGE("Failed to set pragmas: %s", err_msg);
        sqlite3_free(err_msg);
    }

    if (sqlite3_exec(db, DB_SCHEMA.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        LOGE("Failed to create schema: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return nullptr;
    }

    if (!init_face_schema(db)) {
        LOGE("Warning: init_face_schema failed to initialize face schema.");
    }

    if (!init_graph_schema(db)) {
        LOGE("Warning: init_graph_schema failed to initialize graph schema.");
    }

    // Removed accidental WIPE

    return db;
}

bool insert_file(sqlite3* db, const file_meta& file)
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO files (
            abs_path, display_name, filetype, mime_type, size_bytes, mtime_unix,
            last_indexed_at, width_px, height_px, duration_ms, status, retry_count
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, file.content_uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file.file_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, file.mime_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, file.size_bytes);
    sqlite3_bind_int64(stmt, 6, file.mtime_unix);
    sqlite3_bind_int64(stmt, 7, file.last_indexed_at);
    sqlite3_bind_int(stmt, 8, file.width_px);
    sqlite3_bind_int(stmt, 9, file.height_px);
    sqlite3_bind_int64(stmt, 10, file.duration_ms);
    sqlite3_bind_text(stmt, 11, file.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, file.retry_count);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool insert_files(const std::vector<file_meta>& files, const std::string& db_path)
{
    sqlite3* db = initialize_database(db_path);
    if (!db) return false;

    char* err_msg = nullptr;
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);

    int success_count = 0;
    for (const auto& file : files)
    {
        if (insert_file(db, file)) success_count++;
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
    LOGI("Batch insert complete. Success: %d/%zu", success_count, files.size());

    sqlite3_close(db);
    return true;
}

std::vector<file_meta> get_files(const std::string& db_path, int limit)
{
    std::vector<file_meta> results;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        LOGE("Failed to open DB for reading: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, abs_path, display_name, filetype, mime_type, size_bytes, mtime_unix,
               last_indexed_at, width_px, height_px, duration_ms, status, retry_count
        FROM files
        LIMIT ?;
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for get_files: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return results;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        file_meta meta;

        meta.id = sqlite3_column_int64(stmt, 0);

        const char* uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        meta.content_uri = uri ? uri : "";

        const char* dname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        meta.display_name = dname ? dname : "";

        const char* ftype = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        meta.file_type = ftype ? ftype : "";

        const char* mime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        meta.mime_type = mime ? mime : "";

        meta.size_bytes     = sqlite3_column_int64(stmt, 5);
        meta.mtime_unix     = sqlite3_column_int64(stmt, 6);
        meta.last_indexed_at = sqlite3_column_int64(stmt, 7);
        meta.width_px       = sqlite3_column_int(stmt, 8);
        meta.height_px      = sqlite3_column_int(stmt, 9);
        meta.duration_ms    = sqlite3_column_int64(stmt, 10);

        const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        meta.status = status ? status : "pending";

        meta.retry_count = sqlite3_column_int(stmt, 12);

        results.push_back(meta);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    LOGI("get_files returned %zu rows", results.size());
    return results;
}

int get_file_count(const std::string& db_path)
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        LOGE("Failed to open DB for count: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM files;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for get_file_count: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    LOGI("get_file_count = %d", count);
    return count;
}

std::string get_database_stats_json(const std::string& db_path)
{
    sqlite3* db = nullptr;
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        return "{}";
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT filetype, COUNT(*), SUM(size_bytes) FROM files GROUP BY filetype;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        return "{}";
    }

    std::string json = "{";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* ftype = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int count = sqlite3_column_int(stmt, 1);
        int64_t size_bytes = sqlite3_column_int64(stmt, 2);

        if (ftype) {
            if (!first) json += ", ";
            json += "\"" + std::string(ftype) + "\": {\"count\": " + std::to_string(count) + ", \"size\": " + std::to_string(size_bytes) + "}";
            first = false;
        }
    }
    
    // Add clip_vec and target_files count
    int clip_vec_count = 0;
    sqlite3_stmt* stmt1 = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM clip_vec;", -1, &stmt1, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt1) == SQLITE_ROW) {
            clip_vec_count = sqlite3_column_int(stmt1, 0);
        }
        sqlite3_finalize(stmt1);
    }

    int target_files_count = 0;
    sqlite3_stmt* stmt2 = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM files WHERE status != 'skipped';", -1, &stmt2, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt2) == SQLITE_ROW) {
            target_files_count = sqlite3_column_int(stmt2, 0);
        }
        sqlite3_finalize(stmt2);
    }
    
    if (!first) json += ", ";
    json += "\"clip_vec_count\": " + std::to_string(clip_vec_count) + ", ";
    json += "\"target_files_count\": " + std::to_string(target_files_count);

    json += "}";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return json;
}