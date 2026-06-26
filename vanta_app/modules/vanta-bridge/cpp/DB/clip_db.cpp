#include "clip_db.hpp"
#include <iostream>
#include <android/log.h>
#define SQLITE_CORE 1
#include "sqlite-vec.h"

#define LOG_TAG "VantaClipDB"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

bool init_clip_db(sqlite3* db)
{
    char* err_msg = nullptr;
    const char* sql = "CREATE VIRTUAL TABLE IF NOT EXISTS clip_vec USING vec0(file_id INTEGER PRIMARY KEY, embedding FLOAT[512]);";
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        LOGE("Failed to create clip_vec table: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

std::vector<file_meta> get_unindexed_images(const std::string& db_path, int limit)
{
    std::vector<file_meta> results;

    sqlite3* db = nullptr;
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        LOGE("Failed to open DB for reading unindexed images: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, abs_path, display_name, filetype, mime_type, size_bytes, mtime_unix,
               last_indexed_at, width_px, height_px, duration_ms, status, retry_count
        FROM files
        WHERE filetype = 'picture' AND status = 'pending'
        LIMIT ?;
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for get_unindexed_images: %s", sqlite3_errmsg(db));
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

    return results;
}

int get_picture_count(const std::string& db_path)
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM files WHERE filetype = 'picture';";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
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

    return count;
}

int get_unindexed_picture_count(const std::string& db_path)
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM files WHERE filetype = 'picture' AND status = 'pending';";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
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

    return count;
}

bool save_clip_embedding(sqlite3* db, int64_t file_id, const std::vector<float>& embedding)
{
    sqlite3_stmt* stmt = nullptr;
    // clip_vec requires file_id as PRIMARY KEY, and embedding
    const char* sql_vec = "INSERT OR REPLACE INTO clip_vec(file_id, embedding) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql_vec, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for save_clip_embedding: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_blob(stmt, 2, embedding.data(), embedding.size() * sizeof(float), SQLITE_TRANSIENT);
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!success) {
        LOGE("Failed to insert embedding into clip_vec");
        return false;
    }

    // Now update files table
    const char* sql_upd = "UPDATE files SET status = 'indexed', last_indexed_at = strftime('%s','now') WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_upd, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for update file status: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, file_id);
    success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    return success;
}

bool update_file_status(sqlite3* db, int64_t file_id, const std::string& status)
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE files SET status = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOGE("Prepare failed for update_file_status: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, file_id);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    return success;
}
