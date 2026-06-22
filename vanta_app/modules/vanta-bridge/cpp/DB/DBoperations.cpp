#include "DBoperations.hpp"
#include <iostream>
#include <android/log.h>

#define LOG_TAG "VantaDB"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

sqlite3* initialize_database(
    const std::string& db_path)
{
    sqlite3* db = nullptr;

    const std::string DB_SCHEMA = R"(
CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY,
    content_uri     TEXT NOT NULL UNIQUE,
    filetype        TEXT NOT NULL
                    CHECK(filetype IN ('image','document','audio','video')),
    mime_type       TEXT,
    size_bytes      INTEGER,
    mtime_unix      INTEGER NOT NULL,
    last_indexed_at INTEGER,
    width_px        INTEGER,
    height_px       INTEGER,
    duration_ms     INTEGER,
    status          TEXT NOT NULL DEFAULT 'pending'
                    CHECK(status IN ('pending','indexed','failed','skipped')),
    retry_count     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_files_filetype ON files(filetype);
CREATE INDEX IF NOT EXISTS idx_files_mtime ON files(mtime_unix);
CREATE INDEX IF NOT EXISTS idx_files_status ON files(status);
)";

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

    return db;
}

bool insert_file(sqlite3* db, const file_meta& file)
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO files (
            content_uri, filetype, mime_type, size_bytes, mtime_unix,
            last_indexed_at, width_px, height_px, duration_ms, status, retry_count
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, file.content_uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.file_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file.mime_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, file.size_bytes);
    sqlite3_bind_int64(stmt, 5, file.mtime_unix);
    sqlite3_bind_int64(stmt, 6, file.last_indexed_at);
    sqlite3_bind_int(stmt, 7, file.width_px);
    sqlite3_bind_int(stmt, 8, file.height_px);
    sqlite3_bind_int64(stmt, 9, file.duration_ms);
    sqlite3_bind_text(stmt, 10, file.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, file.retry_count);

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