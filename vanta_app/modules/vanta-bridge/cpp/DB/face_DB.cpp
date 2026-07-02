#include "face_DB.hpp"
#include <android/log.h>
#include <fstream>
#include <ctime>
#include <opencv2/imgcodecs.hpp>

#define LOG_TAG "VantaFaceDB"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool init_face_schema(sqlite3* db) {
    const char* ddl1 = "CREATE TABLE IF NOT EXISTS entities (entity_id INTEGER PRIMARY KEY AUTOINCREMENT, entity_type TEXT NOT NULL CHECK(entity_type IN ('person')), display_name TEXT, confidence REAL NOT NULL DEFAULT 1.0, sample_count INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)";
    const char* ddl2 = "CREATE VIRTUAL TABLE IF NOT EXISTS person_centroids USING vec0(entity_id INTEGER PRIMARY KEY, embedding FLOAT[512])";
    const char* ddl3 = "CREATE TABLE IF NOT EXISTS face_detections (id INTEGER PRIMARY KEY AUTOINCREMENT, file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, entity_id INTEGER REFERENCES entities(entity_id) ON DELETE SET NULL, bbox_x INTEGER NOT NULL, bbox_y INTEGER NOT NULL, bbox_w INTEGER NOT NULL, bbox_h INTEGER NOT NULL, det_score REAL NOT NULL, created_at INTEGER NOT NULL, aligned_crop_path TEXT)";
    const char* ddl4 = "CREATE VIRTUAL TABLE IF NOT EXISTS face_vec USING vec0(detection_id INTEGER PRIMARY KEY, embedding FLOAT[512])";
    const char* ddl5 = "CREATE TABLE IF NOT EXISTS entity_memberships (entity_id INTEGER NOT NULL, file_id INTEGER NOT NULL, score REAL DEFAULT 1.0, created_at INTEGER NOT NULL, PRIMARY KEY (entity_id, file_id), FOREIGN KEY (entity_id) REFERENCES entities(entity_id) ON DELETE CASCADE, FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE)";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, ddl1, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL1: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, ddl2, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL2: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, ddl3, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL3: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    
    // Migration: Add aligned_crop_path column if it doesn't exist
    sqlite3_exec(db, "ALTER TABLE face_detections ADD COLUMN aligned_crop_path TEXT", nullptr, nullptr, nullptr);
    if (sqlite3_exec(db, ddl4, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL4: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, ddl5, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL5: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool save_face_detection(sqlite3* db, int64_t file_id, const FaceResult& face, const std::string& aligned_crop_path, int64_t* out_detection_id) {
    const char* sql = "INSERT INTO face_detections (file_id, entity_id, bbox_x, bbox_y, bbox_w, bbox_h, det_score, created_at, aligned_crop_path) VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare save_face_detection: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_int(stmt, 2, static_cast<int>(face.bbox.x));
    sqlite3_bind_int(stmt, 3, static_cast<int>(face.bbox.y));
    sqlite3_bind_int(stmt, 4, static_cast<int>(face.bbox.width));
    sqlite3_bind_int(stmt, 5, static_cast<int>(face.bbox.height));
    sqlite3_bind_double(stmt, 6, face.confidence);
    sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(std::time(nullptr)));
    sqlite3_bind_text(stmt, 8, aligned_crop_path.c_str(), -1, SQLITE_TRANSIENT);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        *out_detection_id = sqlite3_last_insert_rowid(db);
        success = true;
    } else {
        LOGE("Failed to execute save_face_detection: %s", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool save_face_embedding(sqlite3* db, int64_t detection_id, const std::vector<float>& embedding) {
    const char* sql = "INSERT INTO face_vec (detection_id, embedding) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare save_face_embedding: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, detection_id);
    sqlite3_bind_blob(stmt, 2, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        success = true;
    } else {
        LOGE("Failed to execute save_face_embedding: %s", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool run_face_pipeline(sqlite3* db, const std::string& abs_path, int64_t file_id, Face_embedding& face_model) {
    std::ifstream ifs(abs_path, std::ios::binary);
    if (!ifs.is_open()) {
        LOGE("Failed to open file: %s", abs_path.c_str());
        return false;
    }

    std::vector<char> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (buf.empty()) {
        LOGE("File buffer is empty: %s", abs_path.c_str());
        return false;
    }

    cv::Mat image = cv::imdecode(cv::Mat(1, buf.size(), CV_8UC1, buf.data()), cv::IMREAD_COLOR);
    if (image.empty()) {
        LOGE("Failed to decode image: %s", abs_path.c_str());
        return false;
    }

    std::vector<FaceResult> faces = face_model.get_faces(image);

    const char* sql = "UPDATE files SET face_count = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(faces.size()));
        sqlite3_bind_int64(stmt, 2, file_id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOGE("Failed to update face_count: %s", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("Failed to prepare update face_count: %s", sqlite3_errmsg(db));
    }

    if (faces.empty()) {
        return true;
    }

    std::vector<std::vector<float>> embeddings = face_model.get_embedding(image, faces);

    for (size_t i = 0; i < faces.size(); ++i) {
        std::string crop_path = "";
        try {
            // Save a padded bbox crop from the original image at native resolution.
            // This avoids upscaling small faces (which causes blur with aligned warps).
            const auto& bbox = faces[i].bbox;
            float pad_x = bbox.width * 0.4f;
            float pad_y = bbox.height * 0.4f;
            int x1 = std::max(0, static_cast<int>(bbox.x - pad_x));
            int y1 = std::max(0, static_cast<int>(bbox.y - pad_y));
            int x2 = std::min(image.cols, static_cast<int>(bbox.x + bbox.width + pad_x));
            int y2 = std::min(image.rows, static_cast<int>(bbox.y + bbox.height + pad_y));

            if (x2 > x1 && y2 > y1) {
                cv::Mat face_crop = image(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();

                // Make it square by center-cropping the longer dimension
                int crop_size = std::min(face_crop.cols, face_crop.rows);
                int dx = (face_crop.cols - crop_size) / 2;
                int dy = (face_crop.rows - crop_size) / 2;
                cv::Mat square_crop = face_crop(cv::Rect(dx, dy, crop_size, crop_size));

                int64_t ts = static_cast<int64_t>(std::time(nullptr));
                std::string target_path = VantaConfig::instance().crop_path(std::to_string(file_id) + "_" + std::to_string(i) + "_" + std::to_string(ts) + ".jpg");
                if (cv::imwrite(target_path, square_crop)) {
                    crop_path = target_path;
                } else {
                    LOGE("cv::imwrite failed for crop %s", target_path.c_str());
                }
            }
        } catch(const std::exception& e) {
            LOGE("Exception during align_face or imwrite: %s", e.what());
        }

        int64_t detection_id = 0;
        if (!save_face_detection(db, file_id, faces[i], crop_path, &detection_id)) {
            LOGE("save_face_detection failed for face %zu in %s", i, abs_path.c_str());
            continue;
        }

        if (i < embeddings.size()) {
            if (!save_face_embedding(db, detection_id, embeddings[i])) {
                LOGE("save_face_embedding failed for face %zu in %s", i, abs_path.c_str());
            }
        } else {
            LOGE("Embedding missing for face %zu in %s", i, abs_path.c_str());
        }
    }

    return true;
}

bool cluster_faces_for_file(sqlite3* db, int64_t file_id) {
    if (!db) return false;

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* fetch_sql = R"(
        SELECT d.id, v.embedding, d.det_score
        FROM face_detections d
        JOIN face_vec v ON d.id = v.detection_id
        WHERE d.file_id = ? AND d.entity_id IS NULL
    )";
    
    sqlite3_stmt* fetch_stmt = nullptr;
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &fetch_stmt, nullptr) != SQLITE_OK) {
        LOGE("cluster_faces failed to prepare fetch_sql: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int64(fetch_stmt, 1, file_id);

    struct UnassignedFace {
        int64_t detection_id;
        std::vector<float> embedding;
        float det_score;
    };
    std::vector<UnassignedFace> unassigned_faces;

    while (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
        UnassignedFace face;
        face.detection_id = sqlite3_column_int64(fetch_stmt, 0);
        const void* blob = sqlite3_column_blob(fetch_stmt, 1);
        int bytes = sqlite3_column_bytes(fetch_stmt, 1);
        face.det_score = static_cast<float>(sqlite3_column_double(fetch_stmt, 2));
        if (blob && bytes == 512 * sizeof(float)) {
            face.embedding.resize(512);
            std::memcpy(face.embedding.data(), blob, bytes);
            unassigned_faces.push_back(face);
        }
    }
    sqlite3_finalize(fetch_stmt);

    if (unassigned_faces.empty()) {
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        return true; 
    }

    const float GOOD_THRESHOLD = 1.0f;
    const float BLURRY_THRESHOLD = 1.1f;
    const float BLURRY_DET_SCORE_THRESHOLD = 0.5f;

    sqlite3_stmt* match_stmt = nullptr;
    const char* match_sql = "SELECT entity_id, distance FROM person_centroids WHERE embedding MATCH ? AND k = 1";
    sqlite3_prepare_v2(db, match_sql, -1, &match_stmt, nullptr);

    sqlite3_stmt* insert_entity_stmt = nullptr;
    const char* insert_entity_sql = "INSERT INTO entities (entity_type, created_at, updated_at) VALUES ('person', ?, ?)";
    sqlite3_prepare_v2(db, insert_entity_sql, -1, &insert_entity_stmt, nullptr);

    sqlite3_stmt* insert_centroid_stmt = nullptr;
    const char* insert_centroid_sql = "INSERT INTO person_centroids (entity_id, embedding) VALUES (?, ?)";
    sqlite3_prepare_v2(db, insert_centroid_sql, -1, &insert_centroid_stmt, nullptr);

    sqlite3_stmt* update_centroid_stmt = nullptr;
    const char* update_centroid_sql = "UPDATE person_centroids SET embedding = ? WHERE entity_id = ?";
    sqlite3_prepare_v2(db, update_centroid_sql, -1, &update_centroid_stmt, nullptr);
    
    sqlite3_stmt* get_centroid_stmt = nullptr;
    const char* get_centroid_sql = "SELECT embedding FROM person_centroids WHERE entity_id = ?";
    sqlite3_prepare_v2(db, get_centroid_sql, -1, &get_centroid_stmt, nullptr);

    sqlite3_stmt* update_det_stmt = nullptr;
    const char* update_det_sql = "UPDATE face_detections SET entity_id = ? WHERE id = ?";
    sqlite3_prepare_v2(db, update_det_sql, -1, &update_det_stmt, nullptr);

    sqlite3_stmt* insert_membership_stmt = nullptr;
    const char* insert_membership_sql = "INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at) VALUES (?, ?, ?)";
    sqlite3_prepare_v2(db, insert_membership_sql, -1, &insert_membership_stmt, nullptr);

    sqlite3_stmt* update_entity_count_stmt = nullptr;
    const char* update_entity_count_sql = "UPDATE entities SET sample_count = sample_count + 1, updated_at = ? WHERE entity_id = ?";
    sqlite3_prepare_v2(db, update_entity_count_sql, -1, &update_entity_count_stmt, nullptr);

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    for (const auto& face : unassigned_faces) {
        int64_t best_entity_id = -1;
        float min_distance = 9999.0f;

        if (match_stmt) {
            sqlite3_bind_blob(match_stmt, 1, face.embedding.data(), face.embedding.size() * sizeof(float), SQLITE_STATIC);
            if (sqlite3_step(match_stmt) == SQLITE_ROW) {
                best_entity_id = sqlite3_column_int64(match_stmt, 0);
                min_distance = static_cast<float>(sqlite3_column_double(match_stmt, 1));
            }
            sqlite3_reset(match_stmt);
            sqlite3_clear_bindings(match_stmt);
        }

        int64_t final_entity_id = -1;
        float current_threshold = (face.det_score < BLURRY_DET_SCORE_THRESHOLD) ? BLURRY_THRESHOLD : GOOD_THRESHOLD;

        if (best_entity_id != -1 && min_distance <= current_threshold) {
            final_entity_id = best_entity_id;

            if (get_centroid_stmt) {
                sqlite3_bind_int64(get_centroid_stmt, 1, final_entity_id);
                if (sqlite3_step(get_centroid_stmt) == SQLITE_ROW) {
                    const void* blob = sqlite3_column_blob(get_centroid_stmt, 0);
                    int bytes = sqlite3_column_bytes(get_centroid_stmt, 0);
                    if (blob && bytes == 512 * sizeof(float)) {
                        std::vector<float> old_centroid(512);
                        std::memcpy(old_centroid.data(), blob, bytes);

                        std::vector<float> new_centroid(512);
                        float norm = 0.0f;
                        for (int i = 0; i < 512; ++i) {
                            new_centroid[i] = old_centroid[i] * 0.9f + face.embedding[i] * 0.1f;
                            norm += new_centroid[i] * new_centroid[i];
                        }
                        norm = std::sqrt(norm);
                        if (norm > 0) {
                            for (int i = 0; i < 512; ++i) new_centroid[i] /= norm;
                        }

                        if (update_centroid_stmt) {
                            sqlite3_bind_blob(update_centroid_stmt, 1, new_centroid.data(), 512 * sizeof(float), SQLITE_TRANSIENT);
                            sqlite3_bind_int64(update_centroid_stmt, 2, final_entity_id);
                            sqlite3_step(update_centroid_stmt);
                            sqlite3_reset(update_centroid_stmt);
                            sqlite3_clear_bindings(update_centroid_stmt);
                        }
                    }
                }
                sqlite3_reset(get_centroid_stmt);
                sqlite3_clear_bindings(get_centroid_stmt);
            }
        } else {
            if (face.det_score < BLURRY_DET_SCORE_THRESHOLD) {
                // It's a blurry face and didn't match even with relaxed threshold.
                // Leave entity_id as NULL to be processed in second pass.
                continue;
            }

            if (insert_entity_stmt) {
                sqlite3_bind_int64(insert_entity_stmt, 1, now);
                sqlite3_bind_int64(insert_entity_stmt, 2, now);
                if (sqlite3_step(insert_entity_stmt) == SQLITE_DONE) {
                    final_entity_id = sqlite3_last_insert_rowid(db);
                }
                sqlite3_reset(insert_entity_stmt);
                sqlite3_clear_bindings(insert_entity_stmt);
            }

            if (final_entity_id != -1 && insert_centroid_stmt) {
                sqlite3_bind_int64(insert_centroid_stmt, 1, final_entity_id);
                sqlite3_bind_blob(insert_centroid_stmt, 2, face.embedding.data(), 512 * sizeof(float), SQLITE_STATIC);
                sqlite3_step(insert_centroid_stmt);
                sqlite3_reset(insert_centroid_stmt);
                sqlite3_clear_bindings(insert_centroid_stmt);
            }
        }

        if (final_entity_id != -1) {
            if (update_det_stmt) {
                sqlite3_bind_int64(update_det_stmt, 1, final_entity_id);
                sqlite3_bind_int64(update_det_stmt, 2, face.detection_id);
                sqlite3_step(update_det_stmt);
                sqlite3_reset(update_det_stmt);
                sqlite3_clear_bindings(update_det_stmt);
            }

            if (insert_membership_stmt) {
                sqlite3_bind_int64(insert_membership_stmt, 1, final_entity_id);
                sqlite3_bind_int64(insert_membership_stmt, 2, file_id);
                sqlite3_bind_int64(insert_membership_stmt, 3, now);
                sqlite3_step(insert_membership_stmt);
                sqlite3_reset(insert_membership_stmt);
                sqlite3_clear_bindings(insert_membership_stmt);
            }

            if (update_entity_count_stmt) {
                sqlite3_bind_int64(update_entity_count_stmt, 1, now);
                sqlite3_bind_int64(update_entity_count_stmt, 2, final_entity_id);
                sqlite3_step(update_entity_count_stmt);
                sqlite3_reset(update_entity_count_stmt);
                sqlite3_clear_bindings(update_entity_count_stmt);
            }
        }
    }

    if (match_stmt) sqlite3_finalize(match_stmt);
    if (insert_entity_stmt) sqlite3_finalize(insert_entity_stmt);
    if (insert_centroid_stmt) sqlite3_finalize(insert_centroid_stmt);
    if (update_centroid_stmt) sqlite3_finalize(update_centroid_stmt);
    if (get_centroid_stmt) sqlite3_finalize(get_centroid_stmt);
    if (update_det_stmt) sqlite3_finalize(update_det_stmt);
    if (insert_membership_stmt) sqlite3_finalize(insert_membership_stmt);
    if (update_entity_count_stmt) sqlite3_finalize(update_entity_count_stmt);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

void recluster_pending_faces(sqlite3* db) {
    if (!db) return;

    LOGI("Starting second pass to recluster pending (blurry) faces...");

    const char* fetch_sql = R"(
        SELECT d.id, d.file_id, v.embedding
        FROM face_detections d
        JOIN face_vec v ON d.id = v.detection_id
        WHERE d.entity_id IS NULL
    )";

    sqlite3_stmt* fetch_stmt = nullptr;
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &fetch_stmt, nullptr) != SQLITE_OK) {
        LOGE("recluster_pending failed to prepare fetch_sql: %s", sqlite3_errmsg(db));
        return;
    }

    struct PendingFace {
        int64_t detection_id;
        int64_t file_id;
        std::vector<float> embedding;
    };
    std::vector<PendingFace> pending_faces;

    while (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
        PendingFace face;
        face.detection_id = sqlite3_column_int64(fetch_stmt, 0);
        face.file_id = sqlite3_column_int64(fetch_stmt, 1);
        const void* blob = sqlite3_column_blob(fetch_stmt, 2);
        int bytes = sqlite3_column_bytes(fetch_stmt, 2);
        if (blob && bytes == 512 * sizeof(float)) {
            face.embedding.resize(512);
            std::memcpy(face.embedding.data(), blob, bytes);
            pending_faces.push_back(face);
        }
    }
    sqlite3_finalize(fetch_stmt);

    if (pending_faces.empty()) {
        LOGI("No pending faces to recluster.");
        return;
    }

    LOGI("Found %zu pending faces to recluster.", pending_faces.size());

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const float BLURRY_THRESHOLD = 1.1f;

    sqlite3_stmt* match_stmt = nullptr;
    const char* match_sql = "SELECT entity_id, distance FROM person_centroids WHERE embedding MATCH ? AND k = 1";
    sqlite3_prepare_v2(db, match_sql, -1, &match_stmt, nullptr);

    sqlite3_stmt* update_det_stmt = nullptr;
    const char* update_det_sql = "UPDATE face_detections SET entity_id = ? WHERE id = ?";
    sqlite3_prepare_v2(db, update_det_sql, -1, &update_det_stmt, nullptr);

    sqlite3_stmt* insert_membership_stmt = nullptr;
    const char* insert_membership_sql = "INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at) VALUES (?, ?, ?)";
    sqlite3_prepare_v2(db, insert_membership_sql, -1, &insert_membership_stmt, nullptr);

    sqlite3_stmt* update_entity_count_stmt = nullptr;
    const char* update_entity_count_sql = "UPDATE entities SET sample_count = sample_count + 1, updated_at = ? WHERE entity_id = ?";
    sqlite3_prepare_v2(db, update_entity_count_sql, -1, &update_entity_count_stmt, nullptr);

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    for (const auto& face : pending_faces) {
        int64_t best_entity_id = -1;
        float min_distance = 9999.0f;

        if (match_stmt) {
            sqlite3_bind_blob(match_stmt, 1, face.embedding.data(), face.embedding.size() * sizeof(float), SQLITE_STATIC);
            if (sqlite3_step(match_stmt) == SQLITE_ROW) {
                best_entity_id = sqlite3_column_int64(match_stmt, 0);
                min_distance = static_cast<float>(sqlite3_column_double(match_stmt, 1));
            }
            sqlite3_reset(match_stmt);
            sqlite3_clear_bindings(match_stmt);
        }

        int64_t final_entity_id = -1; // -1 means ignore permanently

        if (best_entity_id != -1 && min_distance <= BLURRY_THRESHOLD) {
            final_entity_id = best_entity_id;
            
            // Note: We don't update the centroid with poor-quality face embeddings here to avoid polluting the centroid.

            if (insert_membership_stmt) {
                sqlite3_bind_int64(insert_membership_stmt, 1, final_entity_id);
                sqlite3_bind_int64(insert_membership_stmt, 2, face.file_id);
                sqlite3_bind_int64(insert_membership_stmt, 3, now);
                sqlite3_step(insert_membership_stmt);
                sqlite3_reset(insert_membership_stmt);
                sqlite3_clear_bindings(insert_membership_stmt);
            }

            if (update_entity_count_stmt) {
                sqlite3_bind_int64(update_entity_count_stmt, 1, now);
                sqlite3_bind_int64(update_entity_count_stmt, 2, final_entity_id);
                sqlite3_step(update_entity_count_stmt);
                sqlite3_reset(update_entity_count_stmt);
                sqlite3_clear_bindings(update_entity_count_stmt);
            }
        }

        if (update_det_stmt) {
            sqlite3_bind_int64(update_det_stmt, 1, final_entity_id);
            sqlite3_bind_int64(update_det_stmt, 2, face.detection_id);
            sqlite3_step(update_det_stmt);
            sqlite3_reset(update_det_stmt);
            sqlite3_clear_bindings(update_det_stmt);
        }
    }

    if (match_stmt) sqlite3_finalize(match_stmt);
    if (update_det_stmt) sqlite3_finalize(update_det_stmt);
    if (insert_membership_stmt) sqlite3_finalize(insert_membership_stmt);
    if (update_entity_count_stmt) sqlite3_finalize(update_entity_count_stmt);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    LOGI("Second pass reclustering completed.");
}

void merge_similar_entities(sqlite3* db) {
    if (!db) return;

    LOGI("Starting entity merge pass...");

    const float MERGE_THRESHOLD = 1.0f;  // L2 distance for centroids (cos_sim >= 0.5)

    // Fetch all entity centroids with their sample counts
    struct EntityCentroid {
        int64_t entity_id;
        int sample_count;
        std::vector<float> embedding;
        bool merged = false;
    };

    const char* fetch_sql = R"(
        SELECT e.entity_id, e.sample_count, pc.embedding
        FROM entities e
        JOIN person_centroids pc ON e.entity_id = pc.entity_id
        ORDER BY e.sample_count DESC
    )";

    sqlite3_stmt* fetch_stmt = nullptr;
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &fetch_stmt, nullptr) != SQLITE_OK) {
        LOGE("merge_similar_entities: failed to prepare fetch: %s", sqlite3_errmsg(db));
        return;
    }

    std::vector<EntityCentroid> all_entities;
    while (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
        EntityCentroid ec;
        ec.entity_id = sqlite3_column_int64(fetch_stmt, 0);
        ec.sample_count = sqlite3_column_int(fetch_stmt, 1);
        const void* blob = sqlite3_column_blob(fetch_stmt, 2);
        int bytes = sqlite3_column_bytes(fetch_stmt, 2);
        if (blob && bytes == 512 * sizeof(float)) {
            ec.embedding.resize(512);
            std::memcpy(ec.embedding.data(), blob, bytes);
            all_entities.push_back(ec);
        }
    }
    sqlite3_finalize(fetch_stmt);

    if (all_entities.size() < 2) {
        LOGI("Not enough entities to merge (%zu).", all_entities.size());
        return;
    }

    // Greedy merge: for each entity (sorted by sample_count desc), find nearest
    // un-merged entity. If distance < threshold, merge smaller into larger.
    int merge_count = 0;

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    for (size_t i = 0; i < all_entities.size(); ++i) {
        if (all_entities[i].merged) continue;

        for (size_t j = i + 1; j < all_entities.size(); ++j) {
            if (all_entities[j].merged) continue;

            // Compute L2 distance between centroids
            float dist_sq = 0.0f;
            for (int d = 0; d < 512; ++d) {
                float diff = all_entities[i].embedding[d] - all_entities[j].embedding[d];
                dist_sq += diff * diff;
            }
            float dist = std::sqrt(dist_sq);

            if (dist <= MERGE_THRESHOLD) {
                int64_t keeper_id = all_entities[i].entity_id;
                int64_t merged_id = all_entities[j].entity_id;

                LOGI("Merging entity %ld into %ld (distance=%.4f)",
                     (long)merged_id, (long)keeper_id, dist);

                // Move face_detections
                const char* move_det_sql = "UPDATE face_detections SET entity_id = ? WHERE entity_id = ?";
                sqlite3_stmt* s = nullptr;
                sqlite3_prepare_v2(db, move_det_sql, -1, &s, nullptr);
                sqlite3_bind_int64(s, 1, keeper_id);
                sqlite3_bind_int64(s, 2, merged_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                // Move entity_memberships
                const char* move_mem_sql = "INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at) SELECT ?, file_id, created_at FROM entity_memberships WHERE entity_id = ?";
                sqlite3_prepare_v2(db, move_mem_sql, -1, &s, nullptr);
                sqlite3_bind_int64(s, 1, keeper_id);
                sqlite3_bind_int64(s, 2, merged_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                const char* del_mem_sql = "DELETE FROM entity_memberships WHERE entity_id = ?";
                sqlite3_prepare_v2(db, del_mem_sql, -1, &s, nullptr);
                sqlite3_bind_int64(s, 1, merged_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                // Recompute keeper centroid as weighted average
                float total = (float)(all_entities[i].sample_count + all_entities[j].sample_count);
                float w_i = (float)all_entities[i].sample_count / total;
                float w_j = (float)all_entities[j].sample_count / total;
                std::vector<float> new_centroid(512);
                float norm = 0.0f;
                for (int d = 0; d < 512; ++d) {
                    new_centroid[d] = all_entities[i].embedding[d] * w_i + all_entities[j].embedding[d] * w_j;
                    norm += new_centroid[d] * new_centroid[d];
                }
                norm = std::sqrt(norm);
                if (norm > 0) {
                    for (int d = 0; d < 512; ++d) new_centroid[d] /= norm;
                }

                // Update keeper centroid
                const char* upd_cent_sql = "UPDATE person_centroids SET embedding = ? WHERE entity_id = ?";
                sqlite3_prepare_v2(db, upd_cent_sql, -1, &s, nullptr);
                sqlite3_bind_blob(s, 1, new_centroid.data(), 512 * sizeof(float), SQLITE_TRANSIENT);
                sqlite3_bind_int64(s, 2, keeper_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                // Update keeper sample_count
                int new_count = all_entities[i].sample_count + all_entities[j].sample_count;
                int64_t now = static_cast<int64_t>(std::time(nullptr));
                const char* upd_ent_sql = "UPDATE entities SET sample_count = ?, updated_at = ? WHERE entity_id = ?";
                sqlite3_prepare_v2(db, upd_ent_sql, -1, &s, nullptr);
                sqlite3_bind_int(s, 1, new_count);
                sqlite3_bind_int64(s, 2, now);
                sqlite3_bind_int64(s, 3, keeper_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                // Delete merged entity centroid and entity row
                const char* del_cent_sql = "DELETE FROM person_centroids WHERE entity_id = ?";
                sqlite3_prepare_v2(db, del_cent_sql, -1, &s, nullptr);
                sqlite3_bind_int64(s, 1, merged_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                const char* del_ent_sql = "DELETE FROM entities WHERE entity_id = ?";
                sqlite3_prepare_v2(db, del_ent_sql, -1, &s, nullptr);
                sqlite3_bind_int64(s, 1, merged_id);
                sqlite3_step(s);
                sqlite3_finalize(s);

                // Update in-memory state
                all_entities[i].embedding = new_centroid;
                all_entities[i].sample_count = new_count;
                all_entities[j].merged = true;
                merge_count++;
            }
        }
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    LOGI("Entity merge pass completed: %d merges performed.", merge_count);
}

bool reset_face_data(sqlite3* db) {
    if (!db) return false;

    LOGI("Resetting all face data for re-indexing...");

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Clear all face-related tables
    sqlite3_exec(db, "DELETE FROM entity_memberships;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM face_vec;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM face_detections;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM person_centroids;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM entities;", nullptr, nullptr, nullptr);

    // Reset face_count so face pipeline re-runs for all images
    sqlite3_exec(db, "UPDATE files SET face_count = NULL;", nullptr, nullptr, nullptr);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    LOGI("Face data reset complete.");
    return true;
}
