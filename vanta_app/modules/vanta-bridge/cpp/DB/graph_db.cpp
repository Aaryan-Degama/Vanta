#include <sqlite3.h>
#include <string>
#include <android/log.h>
#include <ctime>
#include <algorithm>
#include "graph_db.hpp"

#define LOG_TAG "VantaGraphDB"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool init_graph_schema(sqlite3* db) {
    char* err_msg = nullptr;

    const char* stmt1 = R"(
CREATE TABLE IF NOT EXISTS entity_relations (
    entity_a INTEGER NOT NULL, 
    entity_b INTEGER NOT NULL, 
    co_occurrence_count INTEGER NOT NULL DEFAULT 1, 
    first_seen_at INTEGER NOT NULL, 
    last_seen_at INTEGER NOT NULL, 
    PRIMARY KEY (entity_a, entity_b), 
    CHECK (entity_a < entity_b), 
    FOREIGN KEY (entity_a) REFERENCES entities(entity_id) ON DELETE CASCADE, 
    FOREIGN KEY (entity_b) REFERENCES entities(entity_id) ON DELETE CASCADE
);
)";
    if (sqlite3_exec(db, stmt1, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create entity_relations table: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    const char* stmt2 = "CREATE INDEX IF NOT EXISTS idx_er_a ON entity_relations(entity_a);";
    if (sqlite3_exec(db, stmt2, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create idx_er_a index: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    const char* stmt3 = "CREATE INDEX IF NOT EXISTS idx_er_b ON entity_relations(entity_b);";
    if (sqlite3_exec(db, stmt3, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create idx_er_b index: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    const char* stmt4 = R"(
CREATE TABLE IF NOT EXISTS entity_relation_files (
    entity_a INTEGER NOT NULL, 
    entity_b INTEGER NOT NULL, 
    file_id INTEGER NOT NULL, 
    PRIMARY KEY (entity_a, entity_b, file_id), 
    FOREIGN KEY (entity_a, entity_b) REFERENCES entity_relations(entity_a, entity_b) ON DELETE CASCADE, 
    FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE
);
)";
    if (sqlite3_exec(db, stmt4, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create entity_relation_files table: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    const char* stmt5 = "CREATE INDEX IF NOT EXISTS idx_erf_file ON entity_relation_files(file_id);";
    if (sqlite3_exec(db, stmt5, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create idx_erf_file index: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    const char* stmt6 = "CREATE INDEX IF NOT EXISTS idx_erf_pair ON entity_relation_files(entity_a, entity_b);";
    if (sqlite3_exec(db, stmt6, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to create idx_erf_pair index: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    return true;
}

std::vector<EntityMeta> get_top_entities(sqlite3* db, int limit) {
    std::vector<EntityMeta> results;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT entity_id, display_name, sample_count, confidence FROM entities WHERE entity_type = 'person' ORDER BY sample_count DESC LIMIT ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare get_top_entities query: %s", sqlite3_errmsg(db));
        return results;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EntityMeta meta;
        meta.entity_id = sqlite3_column_int64(stmt, 0);
        
        const char* dname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        meta.display_name = dname ? dname : "";
        
        meta.sample_count = sqlite3_column_int(stmt, 2);
        meta.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
        
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

FaceCrop get_best_face_crop(sqlite3* db, int64_t entity_id) {
    FaceCrop crop;
    crop.file_id = -1;
    crop.abs_path = "";
    crop.bbox_x = 0;
    crop.bbox_y = 0;
    crop.bbox_w = 0;
    crop.bbox_h = 0;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT fd.file_id, f.abs_path, fd.bbox_x, fd.bbox_y, fd.bbox_w, fd.bbox_h FROM face_detections fd JOIN files f ON f.id = fd.file_id WHERE fd.entity_id = ? ORDER BY fd.det_score DESC LIMIT 1";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare get_best_face_crop query: %s", sqlite3_errmsg(db));
        return crop;
    }

    sqlite3_bind_int64(stmt, 1, entity_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        crop.file_id = sqlite3_column_int64(stmt, 0);
        
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        crop.abs_path = path ? path : "";
        
        crop.bbox_x = sqlite3_column_int(stmt, 2);
        crop.bbox_y = sqlite3_column_int(stmt, 3);
        crop.bbox_w = sqlite3_column_int(stmt, 4);
        crop.bbox_h = sqlite3_column_int(stmt, 5);
    }

    sqlite3_finalize(stmt);
    return crop;
}

bool set_entity_name(sqlite3* db, int64_t entity_id, const std::string& name) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE entities SET display_name = ?, updated_at = ? WHERE entity_id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare set_entity_name query: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(std::time(nullptr)));
    sqlite3_bind_int64(stmt, 3, entity_id);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    if (!success) {
        LOGE("Failed to execute set_entity_name query: %s", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool update_graph_for_file(sqlite3* db, int64_t file_id) {
    // Step 1 - collect entities
    std::vector<int64_t> entities;
    sqlite3_stmt* stmt_select = nullptr;
    const char* sql_select = "SELECT DISTINCT entity_id FROM face_detections WHERE file_id = ? AND entity_id IS NOT NULL";
    
    if (sqlite3_prepare_v2(db, sql_select, -1, &stmt_select, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt_select, 1, file_id);
        while (sqlite3_step(stmt_select) == SQLITE_ROW) {
            entities.push_back(sqlite3_column_int64(stmt_select, 0));
        }
    }
    sqlite3_finalize(stmt_select);
    
    if (entities.size() < 2) {
        return true;
    }
    
    // Step 2 - open a transaction
    char* err_msg = nullptr;
    if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to begin transaction: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    
    // Step 3 - prepare statements
    const char* sql_upsert_edge = R"(
INSERT INTO entity_relations (entity_a, entity_b, co_occurrence_count, first_seen_at, last_seen_at) 
VALUES (?, ?, 1, ?, ?) 
ON CONFLICT(entity_a, entity_b) DO UPDATE SET 
co_occurrence_count = co_occurrence_count + 1, last_seen_at = excluded.last_seen_at
)";
    const char* sql_insert_file = "INSERT OR IGNORE INTO entity_relation_files (entity_a, entity_b, file_id) VALUES (?, ?, ?)";
    
    sqlite3_stmt* stmt_upsert_edge = nullptr;
    sqlite3_stmt* stmt_insert_file = nullptr;
    
    if (sqlite3_prepare_v2(db, sql_upsert_edge, -1, &stmt_upsert_edge, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(db, sql_insert_file, -1, &stmt_insert_file, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare graph update statements");
        if (stmt_upsert_edge) sqlite3_finalize(stmt_upsert_edge);
        if (stmt_insert_file) sqlite3_finalize(stmt_insert_file);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    bool success = true;
    
    for (size_t i = 0; i < entities.size(); ++i) {
        for (size_t j = i + 1; j < entities.size(); ++j) {
            int64_t a = std::min(entities[i], entities[j]);
            int64_t b = std::max(entities[i], entities[j]);
            
            // Statement A
            sqlite3_reset(stmt_upsert_edge);
            sqlite3_bind_int64(stmt_upsert_edge, 1, a);
            sqlite3_bind_int64(stmt_upsert_edge, 2, b);
            sqlite3_bind_int64(stmt_upsert_edge, 3, now);
            sqlite3_bind_int64(stmt_upsert_edge, 4, now);
            
            if (sqlite3_step(stmt_upsert_edge) != SQLITE_DONE) {
                success = false;
                break;
            }
            
            // Statement B
            sqlite3_reset(stmt_insert_file);
            sqlite3_bind_int64(stmt_insert_file, 1, a);
            sqlite3_bind_int64(stmt_insert_file, 2, b);
            sqlite3_bind_int64(stmt_insert_file, 3, file_id);
            
            if (sqlite3_step(stmt_insert_file) != SQLITE_DONE) {
                success = false;
                break;
            }
        }
        if (!success) break;
    }
    
    sqlite3_finalize(stmt_upsert_edge);
    sqlite3_finalize(stmt_insert_file);
    
    // Step 4 & 5
    if (!success) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE("Failed to commit graph update transaction: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

std::vector<NeighborResult> get_neighbors(sqlite3* db, int64_t entity_id, int limit) {
    std::vector<NeighborResult> results;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT CASE WHEN er.entity_a = ? THEN er.entity_b ELSE er.entity_a END AS neighbor_id, e.display_name, er.co_occurrence_count FROM entity_relations er JOIN entities e ON e.entity_id = CASE WHEN er.entity_a = ? THEN er.entity_b ELSE er.entity_a END WHERE er.entity_a = ? OR er.entity_b = ? ORDER BY er.co_occurrence_count DESC LIMIT ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare get_neighbors query: %s", sqlite3_errmsg(db));
        return results;
    }
    
    sqlite3_bind_int64(stmt, 1, entity_id);
    sqlite3_bind_int64(stmt, 2, entity_id);
    sqlite3_bind_int64(stmt, 3, entity_id);
    sqlite3_bind_int64(stmt, 4, entity_id);
    sqlite3_bind_int(stmt, 5, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NeighborResult nr;
        nr.neighbor_id = sqlite3_column_int64(stmt, 0);
        
        const char* dname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        nr.display_name = dname ? dname : "";
        
        nr.co_occurrence_count = sqlite3_column_int(stmt, 2);
        
        results.push_back(nr);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<EntityFile> get_entity_files(sqlite3* db, int64_t entity_id, int limit) {
    std::vector<EntityFile> results;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT em.file_id, f.abs_path FROM entity_memberships em JOIN files f ON f.id = em.file_id WHERE em.entity_id = ? ORDER BY em.score DESC LIMIT ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare get_entity_files query: %s", sqlite3_errmsg(db));
        return results;
    }
    
    sqlite3_bind_int64(stmt, 1, entity_id);
    sqlite3_bind_int(stmt, 2, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EntityFile ef;
        ef.file_id = sqlite3_column_int64(stmt, 0);
        
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ef.abs_path = path ? path : "";
        
        results.push_back(ef);
    }
    
    sqlite3_finalize(stmt);
    return results;
}
