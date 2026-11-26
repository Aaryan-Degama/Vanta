#include "DBoperations.hpp"
#include "json_utils.hpp"
#include "clip_db.hpp"
#include "graph_db.hpp"
#include <cctype>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
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

-- Full-text index for documents; rowid = files.id.
CREATE VIRTUAL TABLE IF NOT EXISTS doc_fts USING fts5(name, body, tokenize='unicode61 remove_diacritics 2');
)";

    sqlite3_auto_extension((void (*)())sqlite3_vec_init);

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        LOGE("Failed to open DB: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return nullptr;
    }
    sqlite3_busy_timeout(db, 5000);

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

// Prepares `sql`, binds `id` to ?1, steps once.
static bool exec_with_id(sqlite3* db, const char* sql, int64_t id)
{
    sqlite3_stmt* stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (!ok) LOGE("'%s' failed for file %lld: %s", sql, (long long)id, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return ok;
}

// Removes everything derived from one file (embeddings, faces, document text,
// graph links). Virtual tables don't cascade, so each is cleared explicitly.
// Crop files are collected, not deleted, so a rollback leaves them intact.
static bool purge_file_index(sqlite3* db, int64_t file_id, std::vector<std::string>& crop_paths)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, aligned_crop_path FROM face_detections WHERE file_id = ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("purge: select detections failed: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, file_id);
    std::vector<int64_t> detection_ids;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        detection_ids.push_back(sqlite3_column_int64(stmt, 0));
        const char* crop = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (crop && *crop) crop_paths.emplace_back(crop);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("purge: reading detections failed: %s", sqlite3_errmsg(db));
        return false;
    }

    for (int64_t det_id : detection_ids) {
        if (!exec_with_id(db, "DELETE FROM face_vec WHERE detection_id = ?1;", det_id)) return false;
    }
    return exec_with_id(db, "DELETE FROM clip_vec WHERE file_id = ?1;", file_id)
        && exec_with_id(db, "DELETE FROM doc_fts WHERE rowid = ?1;", file_id)
        && exec_with_id(db, "DELETE FROM face_detections WHERE file_id = ?1;", file_id)
        && exec_with_id(db, "DELETE FROM entity_memberships WHERE file_id = ?1;", file_id)
        && exec_with_id(db, "DELETE FROM entity_relation_files WHERE file_id = ?1;", file_id);
}

// Reconciles the files table with a complete scan: new paths are inserted,
// changed ones (mtime/size) are purged and reset to 'pending', and vanished
// ones are purged and removed. All in one transaction; any failure rolls back.
bool insert_files(const std::vector<file_meta>& files, const std::string& db_path)
{
    sqlite3* db = initialize_database(db_path);
    if (!db) return false;
    init_clip_db(db);  // clip_vec is purged below and may not exist yet

    struct existing_row { int64_t id; std::string filetype; int64_t size; int64_t mtime; };
    std::unordered_map<std::string, existing_row> existing;
    std::unordered_set<std::string> seen, scanned_types;
    std::vector<std::string> crop_paths;
    int inserted = 0, updated = 0, removed = 0;
    bool ok = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) == SQLITE_OK;

    sqlite3_stmt* stmt = nullptr;
    if (ok && sqlite3_prepare_v2(db, "SELECT id, abs_path, filetype, size_bytes, mtime_unix FROM files;",
                                 -1, &stmt, nullptr) == SQLITE_OK) {
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            existing[path ? path : ""] = {sqlite3_column_int64(stmt, 0), type ? type : "",
                                         sqlite3_column_int64(stmt, 3), sqlite3_column_int64(stmt, 4)};
        }
        ok = rc == SQLITE_DONE;
    } else ok = false;
    sqlite3_finalize(stmt); stmt = nullptr;

    if (ok && sqlite3_prepare_v2(db, R"(
            UPDATE files SET size_bytes = ?, mtime_unix = ?, display_name = ?, mime_type = ?,
                width_px = ?, height_px = ?, duration_ms = ?, status = 'pending', retry_count = 0,
                face_count = 0, last_indexed_at = 0
            WHERE id = ?;
        )", -1, &stmt, nullptr) != SQLITE_OK) ok = false;

    for (size_t i = 0; ok && i < files.size(); ++i) {
        const file_meta& file = files[i];
        if (!seen.insert(file.content_uri).second) continue;  // duplicate path in scan
        scanned_types.insert(file.file_type);

        auto it = existing.find(file.content_uri);
        if (it == existing.end()) {
            ok = insert_file(db, file);
            if (ok) inserted++;
            continue;
        }
        if (it->second.mtime == file.mtime_unix && it->second.size == file.size_bytes) continue;

        ok = purge_file_index(db, it->second.id, crop_paths);
        if (!ok) break;
        sqlite3_reset(stmt);
        sqlite3_bind_int64(stmt, 1, file.size_bytes);
        sqlite3_bind_int64(stmt, 2, file.mtime_unix);
        sqlite3_bind_text(stmt, 3, file.display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, file.mime_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, file.width_px);
        sqlite3_bind_int(stmt, 6, file.height_px);
        sqlite3_bind_int64(stmt, 7, file.duration_ms);
        sqlite3_bind_int64(stmt, 8, it->second.id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (ok) updated++;
    }
    sqlite3_finalize(stmt);

    // Only remove rows of a type this scan actually returned: a revoked
    // permission or a null MediaStore cursor must not wipe that type's index
    // (and the user's named people with it).
    if (!files.empty()) {
        for (const auto& [path, row] : existing) {
            if (!ok) break;
            if (seen.count(path) || !scanned_types.count(row.filetype)) continue;
            ok = purge_file_index(db, row.id, crop_paths)
                && exec_with_id(db, "DELETE FROM files WHERE id = ?1;", row.id);
            if (ok) removed++;
        }
    }

    // A photo can fail for a transient reason (e.g. the temp-file I/O error of
    // builds before SQLITE_TEMP_STORE=3); give it a few more tries. It holds no
    // derived rows: its per-photo transaction was rolled back.
    int retried = 0;
    if (ok) {
        ok = sqlite3_exec(db,
                "UPDATE files SET status = 'pending' WHERE status = 'failed' AND retry_count < 3;",
                nullptr, nullptr, nullptr) == SQLITE_OK;
        retried = sqlite3_changes(db);
    }

    if (ok && (updated || removed)) {
        ok = sqlite3_exec(db,
                "UPDATE entities SET sample_count = "
                "(SELECT COUNT(*) FROM face_detections fd WHERE fd.entity_id = entities.entity_id);",
                nullptr, nullptr, nullptr) == SQLITE_OK
            && rebuild_graph(db);
    }

    ok = ok && sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) {
        LOGE("insert_files failed, rolling back: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    } else {
        for (const std::string& p : crop_paths) std::remove(p.c_str());
        LOGI("Index reconciled: %d new, %d changed, %d removed, %d failed retried (scan %zu)",
             inserted, updated, removed, retried, files.size());
    }

    sqlite3_close(db);
    return ok;
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

std::string get_pending_documents_json(const std::string& db_path, int limit)
{
    std::string json = "[";
    sqlite3* db = nullptr;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK &&
        sqlite3_prepare_v2(db,
            "SELECT id, abs_path, display_name FROM files WHERE filetype = 'document' AND status = 'pending' LIMIT ?;",
            -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, limit);
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            if (!first) json += ",";
            first = false;
            json += "{" + json_number_field("id", sqlite3_column_int64(stmt, 0)) + "," +
                    json_string_field("path", path ? path : "") + "," +
                    json_string_field("display_name", name ? name : "") + "}";
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return json + "]";
}

bool save_document_text(const std::string& db_path, int64_t file_id,
                        const std::string& name, const std::string& body)
{
    // Schema already exists (insert_files ran initialize_database during the scan).
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for save_document_text: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    bool ok = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_stmt* stmt = nullptr;
    if (ok && sqlite3_prepare_v2(db, "DELETE FROM doc_fts WHERE rowid = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, file_id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    } else ok = false;
    sqlite3_finalize(stmt); stmt = nullptr;

    if (ok && sqlite3_prepare_v2(db, "INSERT INTO doc_fts(rowid, name, body) VALUES (?, ?, ?);", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, file_id);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, body.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    } else ok = false;
    sqlite3_finalize(stmt); stmt = nullptr;

    if (ok && sqlite3_prepare_v2(db,
            "UPDATE files SET status = 'indexed', last_indexed_at = strftime('%s','now') WHERE id = ?;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, file_id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    } else ok = false;
    sqlite3_finalize(stmt);

    ok = ok && sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) {
        LOGE("save_document_text failed for %lld: %s", (long long)file_id, sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    sqlite3_close(db);
    return ok;
}

std::string fts_query_from_words(const std::string& raw)
{
    // Each word becomes a quoted prefix term ("word"*), ANDed together, so
    // user input can never be parsed as FTS5 syntax (AND/OR/NEAR, column:...).
    std::string out, word;
    auto flush = [&]() {
        if (word.empty()) return;
        if (!out.empty()) out += " ";
        out += "\"" + word + "\"*";
        word.clear();
    };
    for (unsigned char c : raw) {
        if (c >= 0x80 || std::isalnum(c)) word += static_cast<char>(c);
        else flush();
    }
    flush();
    return out;
}

std::string search_documents_json(const std::string& db_path, const std::string& query, int limit)
{
    std::string match = fts_query_from_words(query);
    if (match.empty()) return "[]";

    std::string json = "[";
    sqlite3* db = nullptr;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK &&
        sqlite3_prepare_v2(db, R"(
            SELECT f.id, f.abs_path, f.display_name, f.size_bytes, f.mtime_unix,
                   snippet(doc_fts, 1, '', '', '…', 16)
            FROM doc_fts JOIN files f ON f.id = doc_fts.rowid
            WHERE doc_fts MATCH ?
            ORDER BY bm25(doc_fts, 5.0, 1.0)
            LIMIT ?;
        )", -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, match.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            auto text = [&](int i) {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                return std::string(t ? t : "");
            };
            if (!first) json += ",";
            first = false;
            json += "{" +
                json_number_field("file_id", sqlite3_column_int64(stmt, 0)) + "," +
                json_string_field("abs_path", text(1)) + "," +
                json_string_field("display_name", text(2)) + "," +
                json_number_field("size_bytes", sqlite3_column_int64(stmt, 3)) + "," +
                json_number_field("mtime_unix", sqlite3_column_int64(stmt, 4)) + "," +
                json_string_field("snippet", text(5)) + "}";
        }
    }
    else if (db)
    {
        LOGE("search_documents_json failed: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return json + "]";
}
