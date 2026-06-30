#include "query_engine.hpp"
#include "Preprocessing/CLIP/CLIP_tokenizer.hpp"
#include <iostream>
#include <android/log.h>
#define SQLITE_CORE 1
#include "sqlite-vec.h"
#include <random>

#define LOG_TAG "VantaQueryEngine"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static VantaProductionAnalyzer* g_analyzer = nullptr;

bool init_query_engine() {
    // In the future, this will initialize the text tokenizer and text ONNX model.
    if (!g_analyzer) {
        SymSpellDictionary dict;
        std::vector<std::string> whitelist = {"sun", "moon", "car", "tree", "dog", "cat"}; // Populated for testing
        g_analyzer = new VantaProductionAnalyzer(dict, whitelist);
    }
    LOGI("Query engine initialized.");
    return true;
}

std::string get_corrected_query(const std::string& raw_query) {
    if (!g_analyzer) {
        init_query_engine();
    }
    QueryAnalysis analysis = g_analyzer->analyze(raw_query);
    if (analysis.was_corrected) {
        LOGI("Query corrected: %s -> %s", raw_query.c_str(), analysis.corrected_query.c_str());
    }
    return analysis.corrected_query;
}

std::vector<search_result> search_images(const std::string& db_path, const std::string& raw_query, CLIP_session* clip_session, CLIPTokenizer* tokenizer, int top_k) {
    std::vector<search_result> results;

    if (!clip_session || !clip_session->is_loaded()) {
        LOGE("CLIP session not loaded for text search.");
        return results;
    }
    
    if (!tokenizer || !tokenizer->is_loaded()) {
        LOGE("CLIP tokenizer not loaded for text search.");
        return results;
    }

    // Apply typo rectifier
    std::string corrected_query = get_corrected_query(raw_query);
    LOGI("search_images called: raw_query='%s' corrected_query='%s'", raw_query.c_str(), corrected_query.c_str());

    // Tokenize
    std::vector<int64_t> tokens = tokenizer->encode(corrected_query);
    LOGI("Tokenized query into %zu tokens.", tokens.size());

    LOGI("Generating text embedding for search query...");
    std::vector<float> query_embedding;
    try {
        query_embedding = clip_session->get_text_embedding(tokens);
    } catch(const std::exception& e) {
        LOGE("Failed to get text embedding: %s", e.what());
        return results;
    }
    LOGI("Text embedding generated: %zu dims", query_embedding.size());

    sqlite3* db = nullptr;
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for search: %s", sqlite3_errmsg(db));
        return results;
    }
    LOGI("DB opened for search at: %s", db_path.c_str());

    // ── Diagnostic: check if clip_vec has any rows ──
    {
        sqlite3_stmt* cnt_stmt = nullptr;
        // vec0 tables support rowid queries, use that to check for data
        const char* cnt_sql = "SELECT count(*) FROM clip_vec;";
        if (sqlite3_prepare_v2(db, cnt_sql, -1, &cnt_stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(cnt_stmt) == SQLITE_ROW) {
                int vec_count = sqlite3_column_int(cnt_stmt, 0);
                LOGI("clip_vec row count = %d", vec_count);
            }
            sqlite3_finalize(cnt_stmt);
        } else {
            LOGE("Could not count clip_vec rows: %s", sqlite3_errmsg(db));
        }
    }

    // ── Step 1: KNN query on clip_vec ONLY (no JOIN) ──
    struct knn_hit {
        int64_t file_id;
        float distance;
    };
    std::vector<knn_hit> hits;

    {
        const char* knn_sql = "SELECT file_id, distance FROM clip_vec WHERE embedding MATCH ? AND k = ?;";
        sqlite3_stmt* knn_stmt = nullptr;
        if (sqlite3_prepare_v2(db, knn_sql, -1, &knn_stmt, nullptr) != SQLITE_OK) {
            LOGE("KNN prepare failed: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            return results;
        }

        sqlite3_bind_blob(knn_stmt, 1, query_embedding.data(),
                          static_cast<int>(query_embedding.size() * sizeof(float)), SQLITE_STATIC);
        sqlite3_bind_int(knn_stmt, 2, top_k);

        while (sqlite3_step(knn_stmt) == SQLITE_ROW) {
            knn_hit h;
            h.file_id = sqlite3_column_int64(knn_stmt, 0);
            h.distance = static_cast<float>(sqlite3_column_double(knn_stmt, 1));
            hits.push_back(h);
            LOGI("  KNN hit: file_id=%ld distance=%.4f", (long)h.file_id, h.distance);
        }
        sqlite3_finalize(knn_stmt);
    }

    LOGI("KNN returned %zu hits", hits.size());

    // ── Step 2: Look up file details for each hit ──
    {
        const char* file_sql = "SELECT abs_path, display_name, size_bytes, mtime_unix FROM files WHERE id = ?;";
        sqlite3_stmt* file_stmt = nullptr;
        if (sqlite3_prepare_v2(db, file_sql, -1, &file_stmt, nullptr) != SQLITE_OK) {
            LOGE("File lookup prepare failed: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            return results;
        }

        for (const auto& h : hits) {
            sqlite3_reset(file_stmt);
            sqlite3_bind_int64(file_stmt, 1, h.file_id);

            if (sqlite3_step(file_stmt) == SQLITE_ROW) {
                search_result res;
                res.file_id = h.file_id;
                res.distance = h.distance;

                const char* path = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 0));
                res.abs_path = path ? path : "";

                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 1));
                res.display_name = name ? name : "";

                res.size_bytes = sqlite3_column_int64(file_stmt, 2);
                res.mtime_unix = sqlite3_column_int64(file_stmt, 3);

                results.push_back(res);
            }
        }
        sqlite3_finalize(file_stmt);
    }

    LOGI("Search complete: %zu results with file details", results.size());

    sqlite3_close(db);
    return results;
}

