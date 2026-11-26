#include "face_DB.hpp"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ctime>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <opencv2/imgcodecs.hpp>

#define LOG_TAG "VantaFaceDB"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Clustering thresholds as cosine similarity. Taken from general ArcFace practice; NOT measured for this INT8-quantised model.
static const float ASSIGN_COS = 0.50f;  // face -> person centroid
static const float MERGE_COS = 0.60f;   // centroid -> centroid; stricter than assign because a merge is irreversible

// The vec0 tables use the default L2 metric; on unit vectors d = sqrt(2 - 2*cos).
static inline float cos_to_l2(float c) { return std::sqrt(std::max(0.f, 2.f - 2.f * c)); }

// L2 distance between two 512-d embeddings.
static float l2_512(const std::vector<float>& a, const std::vector<float>& b) {
    float dist_sq = 0.0f;
    for (int d = 0; d < 512; ++d) {
        float diff = a[d] - b[d];
        dist_sq += diff * diff;
    }
    return std::sqrt(dist_sq);
}

// Minimum evidence for a face to create an entity or move a centroid. Starting points, not tuned.
static const float SEED_MIN_DET_SCORE = 0.65f;
static const int SEED_MIN_FACE_PX = 48;  // shorter bbox side

// ponytail: k = 5 nearest centroids; raise k if photos routinely hold more than 5 look-alike people.
static const char* MATCH_SQL = "SELECT entity_id, distance FROM person_centroids WHERE embedding MATCH ? AND k = 5 ORDER BY distance";
static const char* USED_SQL = "SELECT entity_id FROM face_detections WHERE file_id = ? AND entity_id IS NOT NULL";
static const char* MEMBERSHIP_SQL = "INSERT INTO entity_memberships (entity_id, file_id, score, created_at) VALUES (?, ?, ?, ?) ON CONFLICT(entity_id, file_id) DO UPDATE SET score = MAX(score, excluded.score)";

static sqlite3_stmt* prepare_or_log(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to prepare '%s': %s", sql, sqlite3_errmsg(db));
    }
    return stmt;
}

// Steps a write statement, logs a failure, and resets it for reuse.
static bool step_done(sqlite3* db, sqlite3_stmt* stmt, const char* what) {
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) LOGE("%s failed: %s", what, sqlite3_errmsg(db));
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return ok;
}

// Entities already assigned to a face of this photo: one person cannot be two faces in one photo.
static bool load_used_entities(sqlite3* db, sqlite3_stmt* used_stmt, int64_t file_id, std::unordered_set<int64_t>& used) {
    sqlite3_bind_int64(used_stmt, 1, file_id);
    int rc;
    while ((rc = sqlite3_step(used_stmt)) == SQLITE_ROW) {
        used.insert(sqlite3_column_int64(used_stmt, 0));
    }
    if (rc != SQLITE_DONE) LOGE("load_used_entities failed: %s", sqlite3_errmsg(db));
    sqlite3_reset(used_stmt);
    sqlite3_clear_bindings(used_stmt);
    return rc == SQLITE_DONE;
}

// Nearest centroid within `threshold` (L2) whose entity is not in `used`; *out_entity_id = -1 if none.
// Returns false only on a query error.
static bool match_entity(sqlite3* db, sqlite3_stmt* match_stmt, const std::vector<float>& embedding,
                         const std::unordered_set<int64_t>& used, float threshold,
                         int64_t* out_entity_id, float* out_distance) {
    *out_entity_id = -1;
    sqlite3_bind_blob(match_stmt, 1, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
    int rc;
    while ((rc = sqlite3_step(match_stmt)) == SQLITE_ROW) {
        float distance = static_cast<float>(sqlite3_column_double(match_stmt, 1));
        if (distance > threshold) break;  // rows arrive nearest first
        int64_t candidate = sqlite3_column_int64(match_stmt, 0);
        if (used.count(candidate)) continue;
        *out_entity_id = candidate;
        *out_distance = distance;
        break;
    }
    bool ok = rc == SQLITE_ROW || rc == SQLITE_DONE;
    if (!ok) LOGE("face centroid match failed: %s", sqlite3_errmsg(db));
    sqlite3_reset(match_stmt);
    sqlite3_clear_bindings(match_stmt);
    return ok;
}

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
    // face_detections is looked up by file (clustering, search verification) and by entity (graph, crops, merge).
    const char* ddl6 = "CREATE INDEX IF NOT EXISTS idx_face_det_file ON face_detections(file_id); CREATE INDEX IF NOT EXISTS idx_face_det_entity ON face_detections(entity_id);";
    if (sqlite3_exec(db, ddl6, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("Failed to execute DDL6: %s", errMsg);
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

bool compute_faces(const std::string& abs_path, int64_t file_id, Face_embedding& face_model, std::vector<ComputedFace>& out) {
    out.clear();
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
    return compute_faces(image, abs_path, file_id, face_model, out);
}

bool compute_faces(const cv::Mat& image, const std::string& abs_path, int64_t /*file_id*/, Face_embedding& face_model, std::vector<ComputedFace>& out) {
    out.clear();
    if (image.empty()) {
        LOGE("Failed to decode image: %s", abs_path.c_str());
        return false;
    }

    std::vector<FaceResult> faces = face_model.get_faces(image);

    // embeddings[i] belongs to faces[i]; a face whose alignment failed has an empty slot.
    std::vector<std::vector<float>> embeddings;
    if (!faces.empty()) embeddings = face_model.get_embedding(image, faces);

    for (size_t i = 0; i < faces.size(); ++i) {
        // A detection without an embedding can never be clustered and would stay pending forever,
        // so no detection row is stored for such a face.
        if (i >= embeddings.size() || embeddings[i].empty()) {
            LOGE("No embedding for face %zu in %s; face skipped", i, abs_path.c_str());
            continue;
        }

        // No crop is written here: get_best_face_crop makes the one avatar per person on demand.
        faces[i].embedding = std::move(embeddings[i]);
        out.push_back({std::move(faces[i])});
    }
    return true;
}

void store_faces(sqlite3* db, int64_t file_id, const std::vector<ComputedFace>& faces) {
    int stored = 0;  // faces saved with both a detection row and an embedding
    for (size_t i = 0; i < faces.size(); ++i) {
        int64_t detection_id = 0;
        // No crop yet; get_best_face_crop fills aligned_crop_path when the avatar is first shown.
        if (!save_face_detection(db, file_id, faces[i].face, "", &detection_id)) {
            LOGE("save_face_detection failed for face %zu of file %lld", i, (long long)file_id);
            continue;
        }

        if (!save_face_embedding(db, detection_id, faces[i].face.embedding)) {
            LOGE("save_face_embedding failed for face %zu of file %lld", i, (long long)file_id);
            // A detection without an embedding would stay pending forever, so it is not kept.
            std::string del_sql = "DELETE FROM face_detections WHERE id = " + std::to_string(detection_id);
            sqlite3_exec(db, del_sql.c_str(), nullptr, nullptr, nullptr);
            continue;
        }
        ++stored;
    }

    // face_count is the number of faces actually stored, so it always matches this file's face_detections rows.
    const char* sql = "UPDATE files SET face_count = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, stored);
        sqlite3_bind_int64(stmt, 2, file_id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOGE("Failed to update face_count: %s", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("Failed to prepare update face_count: %s", sqlite3_errmsg(db));
    }
}

bool cluster_faces_for_file(sqlite3* db, int64_t file_id) {
    if (!db) return false;

    // A savepoint, not BEGIN: it is a transaction of its own when called standalone and nests
    // inside the caller's per-photo transaction.
    if (sqlite3_exec(db, "SAVEPOINT cluster_faces;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("cluster_faces failed to begin transaction: %s", sqlite3_errmsg(db));
        return false;
    }

    // Best detection first, so the clearest face in the photo claims its entity first.
    const char* fetch_sql = R"(
        SELECT d.id, v.embedding, d.det_score, d.bbox_w, d.bbox_h
        FROM face_detections d
        JOIN face_vec v ON d.id = v.detection_id
        WHERE d.file_id = ? AND d.entity_id IS NULL
        ORDER BY d.det_score DESC, d.id
    )";

    sqlite3_stmt* fetch_stmt = nullptr;
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &fetch_stmt, nullptr) != SQLITE_OK) {
        LOGE("cluster_faces failed to prepare fetch_sql: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TO cluster_faces; RELEASE cluster_faces;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int64(fetch_stmt, 1, file_id);

    struct UnassignedFace {
        int64_t detection_id;
        std::vector<float> embedding;
        bool can_seed;  // enough evidence to create an entity / move a centroid
    };
    std::vector<UnassignedFace> unassigned_faces;

    while (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
        UnassignedFace face;
        face.detection_id = sqlite3_column_int64(fetch_stmt, 0);
        const void* blob = sqlite3_column_blob(fetch_stmt, 1);
        int bytes = sqlite3_column_bytes(fetch_stmt, 1);
        float det_score = static_cast<float>(sqlite3_column_double(fetch_stmt, 2));
        int min_side = std::min(sqlite3_column_int(fetch_stmt, 3), sqlite3_column_int(fetch_stmt, 4));
        face.can_seed = det_score >= SEED_MIN_DET_SCORE && min_side >= SEED_MIN_FACE_PX;
        if (blob && bytes == 512 * sizeof(float)) {
            face.embedding.resize(512);
            std::memcpy(face.embedding.data(), blob, bytes);
            unassigned_faces.push_back(face);
        }
    }
    sqlite3_finalize(fetch_stmt);

    if (unassigned_faces.empty()) {
        sqlite3_exec(db, "RELEASE cluster_faces;", nullptr, nullptr, nullptr);
        return true;
    }

    const float assign_threshold = cos_to_l2(ASSIGN_COS);

    sqlite3_stmt* match_stmt = prepare_or_log(db, MATCH_SQL);
    sqlite3_stmt* used_stmt = prepare_or_log(db, USED_SQL);
    sqlite3_stmt* insert_entity_stmt = prepare_or_log(db, "INSERT INTO entities (entity_type, created_at, updated_at) VALUES ('person', ?, ?)");
    sqlite3_stmt* insert_centroid_stmt = prepare_or_log(db, "INSERT INTO person_centroids (entity_id, embedding) VALUES (?, ?)");
    sqlite3_stmt* update_centroid_stmt = prepare_or_log(db, "UPDATE person_centroids SET embedding = ? WHERE entity_id = ?");
    sqlite3_stmt* get_centroid_stmt = prepare_or_log(db, "SELECT pc.embedding, e.sample_count FROM person_centroids pc JOIN entities e ON e.entity_id = pc.entity_id WHERE pc.entity_id = ?");
    sqlite3_stmt* update_det_stmt = prepare_or_log(db, "UPDATE face_detections SET entity_id = ? WHERE id = ?");
    sqlite3_stmt* insert_membership_stmt = prepare_or_log(db, MEMBERSHIP_SQL);
    sqlite3_stmt* update_entity_count_stmt = prepare_or_log(db, "UPDATE entities SET sample_count = sample_count + 1, updated_at = ? WHERE entity_id = ?");

    // Any failure below rolls the whole file back, so its faces stay unassigned instead of half-written.
    bool ok = match_stmt && used_stmt && insert_entity_stmt && insert_centroid_stmt && update_centroid_stmt &&
              get_centroid_stmt && update_det_stmt && insert_membership_stmt && update_entity_count_stmt;

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    std::unordered_set<int64_t> used;
    ok = ok && load_used_entities(db, used_stmt, file_id, used);

    for (const auto& face : unassigned_faces) {
        if (!ok) break;

        int64_t entity_id = -1;
        float distance = 0.0f;
        float score = 1.0f;  // a face that seeds an entity is its own centroid
        ok = match_entity(db, match_stmt, face.embedding, used, assign_threshold, &entity_id, &distance);
        if (!ok) break;

        if (entity_id != -1) {
            score = 1.0f - distance * distance / 2.0f;  // cosine similarity to the centroid

            // Only faces that pass the seed gate may move the centroid.
            if (face.can_seed) {
                std::vector<float> centroid;
                int n = 0;
                sqlite3_bind_int64(get_centroid_stmt, 1, entity_id);
                int rc = sqlite3_step(get_centroid_stmt);
                if (rc == SQLITE_ROW) {
                    const void* blob = sqlite3_column_blob(get_centroid_stmt, 0);
                    int bytes = sqlite3_column_bytes(get_centroid_stmt, 0);
                    n = sqlite3_column_int(get_centroid_stmt, 1);
                    if (blob && bytes == 512 * sizeof(float)) {
                        centroid.resize(512);
                        std::memcpy(centroid.data(), blob, bytes);
                    }
                } else if (rc != SQLITE_DONE) {
                    LOGE("cluster_faces read centroid failed: %s", sqlite3_errmsg(db));
                    ok = false;
                }
                sqlite3_reset(get_centroid_stmt);
                sqlite3_clear_bindings(get_centroid_stmt);

                if (!centroid.empty()) {
                    // Running mean: sample n+1 weighs 1/(n+1), floored so large clusters still adapt.
                    float w = std::max(1.0f / (std::max(n, 0) + 1), 0.05f);
                    float norm = 0.0f;
                    for (int i = 0; i < 512; ++i) {
                        centroid[i] = centroid[i] * (1.0f - w) + face.embedding[i] * w;
                        norm += centroid[i] * centroid[i];
                    }
                    norm = std::sqrt(norm);
                    if (norm > 0) {
                        for (int i = 0; i < 512; ++i) centroid[i] /= norm;
                    }

                    sqlite3_bind_blob(update_centroid_stmt, 1, centroid.data(), 512 * sizeof(float), SQLITE_TRANSIENT);
                    sqlite3_bind_int64(update_centroid_stmt, 2, entity_id);
                    ok = ok && step_done(db, update_centroid_stmt, "cluster_faces update centroid");
                }
            }
        } else if (face.can_seed) {
            sqlite3_bind_int64(insert_entity_stmt, 1, now);
            sqlite3_bind_int64(insert_entity_stmt, 2, now);
            ok = step_done(db, insert_entity_stmt, "cluster_faces insert entity");
            if (ok) {
                entity_id = sqlite3_last_insert_rowid(db);
                sqlite3_bind_int64(insert_centroid_stmt, 1, entity_id);
                sqlite3_bind_blob(insert_centroid_stmt, 2, face.embedding.data(), 512 * sizeof(float), SQLITE_STATIC);
                ok = step_done(db, insert_centroid_stmt, "cluster_faces insert centroid");
            }
        } else {
            // Matches nobody and is too weak to seed a person: leave entity_id NULL (pending)
            // for recluster_pending_faces.
            continue;
        }
        if (!ok) break;

        sqlite3_bind_int64(update_det_stmt, 1, entity_id);
        sqlite3_bind_int64(update_det_stmt, 2, face.detection_id);
        ok = step_done(db, update_det_stmt, "cluster_faces assign detection");

        sqlite3_bind_int64(insert_membership_stmt, 1, entity_id);
        sqlite3_bind_int64(insert_membership_stmt, 2, file_id);
        sqlite3_bind_double(insert_membership_stmt, 3, score);
        sqlite3_bind_int64(insert_membership_stmt, 4, now);
        ok = ok && step_done(db, insert_membership_stmt, "cluster_faces insert membership");

        sqlite3_bind_int64(update_entity_count_stmt, 1, now);
        sqlite3_bind_int64(update_entity_count_stmt, 2, entity_id);
        ok = ok && step_done(db, update_entity_count_stmt, "cluster_faces update sample_count");

        used.insert(entity_id);
    }

    sqlite3_finalize(match_stmt);
    sqlite3_finalize(used_stmt);
    sqlite3_finalize(insert_entity_stmt);
    sqlite3_finalize(insert_centroid_stmt);
    sqlite3_finalize(update_centroid_stmt);
    sqlite3_finalize(get_centroid_stmt);
    sqlite3_finalize(update_det_stmt);
    sqlite3_finalize(insert_membership_stmt);
    sqlite3_finalize(update_entity_count_stmt);

    if (ok && sqlite3_exec(db, "RELEASE cluster_faces;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("cluster_faces failed to commit: %s", sqlite3_errmsg(db));
        ok = false;
    }
    if (!ok) {
        LOGE("cluster_faces rolled back file %lld", (long long)file_id);
        sqlite3_exec(db, "ROLLBACK TO cluster_faces; RELEASE cluster_faces;", nullptr, nullptr, nullptr);
    }
    return ok;
}

void recluster_pending_faces(sqlite3* db) {
    if (!db) return;

    LOGI("Starting second pass to recluster pending faces...");

    const char* fetch_sql = R"(
        SELECT d.id, d.file_id, v.embedding
        FROM face_detections d
        JOIN face_vec v ON d.id = v.detection_id
        WHERE d.entity_id IS NULL
        ORDER BY d.det_score DESC, d.id
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

    const float assign_threshold = cos_to_l2(ASSIGN_COS);

    // Matching (one KNN query per pending face, the slow part) is read-only and runs outside any
    // transaction, so it holds no lock and cannot hit a stale snapshot. Only the proposals are
    // written below, under the write lock.
    struct Proposal {
        const PendingFace* face;
        int64_t entity_id;
        float distance;
    };
    std::vector<Proposal> proposals;
    // Entities proposed in this pass per photo, so two pending faces of one photo never get the same one.
    std::unordered_map<int64_t, std::unordered_set<int64_t>> proposed;

    sqlite3_stmt* match_stmt = prepare_or_log(db, MATCH_SQL);
    sqlite3_stmt* used_stmt = prepare_or_log(db, USED_SQL);
    bool ok = match_stmt && used_stmt;

    for (const auto& face : pending_faces) {
        if (!ok) break;

        std::unordered_set<int64_t>& in_photo = proposed[face.file_id];
        std::unordered_set<int64_t> used = in_photo;
        ok = load_used_entities(db, used_stmt, face.file_id, used);
        if (!ok) break;

        int64_t entity_id = -1;
        float distance = 0.0f;
        ok = match_entity(db, match_stmt, face.embedding, used, assign_threshold, &entity_id, &distance);
        if (!ok) break;

        // No match: entity_id stays NULL (pending) and the face is retried on the next pass.
        if (entity_id == -1) continue;

        proposals.push_back({&face, entity_id, distance});
        in_photo.insert(entity_id);
    }
    sqlite3_finalize(match_stmt);

    if (!ok || proposals.empty()) {
        sqlite3_finalize(used_stmt);
        if (!ok) LOGE("recluster_pending matching failed; pending faces left unassigned");
        else LOGI("Second pass reclustering completed: 0 of %zu pending faces assigned.", pending_faces.size());
        return;
    }

    // IMMEDIATE: the write lock is taken before anything is re-read, so the checks below hold until COMMIT.
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("recluster_pending failed to begin transaction: %s", sqlite3_errmsg(db));
        sqlite3_finalize(used_stmt);
        return;
    }

    sqlite3_stmt* pending_stmt = prepare_or_log(db, "SELECT 1 FROM face_detections WHERE id = ? AND entity_id IS NULL");
    sqlite3_stmt* entity_stmt = prepare_or_log(db, "SELECT 1 FROM entities WHERE entity_id = ?");
    sqlite3_stmt* update_det_stmt = prepare_or_log(db, "UPDATE face_detections SET entity_id = ? WHERE id = ?");
    sqlite3_stmt* insert_membership_stmt = prepare_or_log(db, MEMBERSHIP_SQL);
    sqlite3_stmt* update_entity_count_stmt = prepare_or_log(db, "UPDATE entities SET sample_count = sample_count + 1, updated_at = ? WHERE entity_id = ?");

    // Any failure below rolls the whole pass back; the faces stay pending for the next pass.
    ok = pending_stmt && entity_stmt && update_det_stmt && insert_membership_stmt && update_entity_count_stmt;

    // Steps a "SELECT 1 ... WHERE x = ?" check: 1 = row, 0 = none, -1 = error.
    auto has_row = [&](sqlite3_stmt* stmt, int64_t id) {
        sqlite3_bind_int64(stmt, 1, id);
        int rc = sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        if (rc == SQLITE_ROW) return 1;
        if (rc == SQLITE_DONE) return 0;
        LOGE("recluster_pending check failed: %s", sqlite3_errmsg(db));
        return -1;
    };

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int assigned = 0;
    std::unordered_set<int64_t> used;

    for (const auto& p : proposals) {
        if (!ok) break;

        // Re-checked under the lock: the face is still pending, its entity still exists and is not yet
        // in the photo (the re-read also sees assignments made earlier in this loop). Otherwise the
        // face stays pending for the next pass.
        int still_pending = has_row(pending_stmt, p.face->detection_id);
        int entity_exists = still_pending == 1 ? has_row(entity_stmt, p.entity_id) : 0;
        if (still_pending < 0 || entity_exists < 0) { ok = false; break; }
        if (entity_exists == 0) continue;
        used.clear();
        ok = load_used_entities(db, used_stmt, p.face->file_id, used);
        if (!ok) break;
        if (used.count(p.entity_id)) continue;

        // Note: pending faces failed the seed gate, so they never update the centroid.

        sqlite3_bind_int64(update_det_stmt, 1, p.entity_id);
        sqlite3_bind_int64(update_det_stmt, 2, p.face->detection_id);
        ok = step_done(db, update_det_stmt, "recluster_pending assign detection");

        sqlite3_bind_int64(insert_membership_stmt, 1, p.entity_id);
        sqlite3_bind_int64(insert_membership_stmt, 2, p.face->file_id);
        sqlite3_bind_double(insert_membership_stmt, 3, 1.0f - p.distance * p.distance / 2.0f);  // cosine similarity to the centroid
        sqlite3_bind_int64(insert_membership_stmt, 4, now);
        ok = ok && step_done(db, insert_membership_stmt, "recluster_pending insert membership");

        sqlite3_bind_int64(update_entity_count_stmt, 1, now);
        sqlite3_bind_int64(update_entity_count_stmt, 2, p.entity_id);
        ok = ok && step_done(db, update_entity_count_stmt, "recluster_pending update sample_count");

        if (ok) assigned++;
    }

    sqlite3_finalize(pending_stmt);
    sqlite3_finalize(entity_stmt);
    sqlite3_finalize(used_stmt);
    sqlite3_finalize(update_det_stmt);
    sqlite3_finalize(insert_membership_stmt);
    sqlite3_finalize(update_entity_count_stmt);

    if (ok && sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("recluster_pending failed to commit: %s", sqlite3_errmsg(db));
        ok = false;
    }
    if (!ok) {
        LOGE("recluster_pending rolled back; pending faces left unassigned");
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    LOGI("Second pass reclustering completed: %d of %zu pending faces assigned.", assigned, pending_faces.size());
}

void merge_similar_entities(sqlite3* db, int64_t protected_entity_id) {
    if (!db) return;

    LOGI("Starting entity merge pass...");

    const float MERGE_THRESHOLD = cos_to_l2(MERGE_COS);  // L2 distance between centroids

    // Read-only phase, outside any transaction: the all-pairs centroid scan (O(n^2), the slow part)
    // finds the candidate pairs without holding the write lock. Slots are in greedy order, larger
    // entities first; near[i] lists the later slots within the threshold of slot i.
    std::vector<int64_t> slot_ids;
    std::vector<std::vector<size_t>> near;
    {
        sqlite3_stmt* scan_stmt = prepare_or_log(db, R"(
            SELECT e.entity_id, pc.embedding
            FROM entities e
            JOIN person_centroids pc ON e.entity_id = pc.entity_id
            ORDER BY e.sample_count DESC, e.entity_id
        )");
        if (!scan_stmt) return;
        std::vector<std::vector<float>> embeddings;
        int rc;
        while ((rc = sqlite3_step(scan_stmt)) == SQLITE_ROW) {
            const float* blob = static_cast<const float*>(sqlite3_column_blob(scan_stmt, 1));
            if (blob && sqlite3_column_bytes(scan_stmt, 1) == 512 * sizeof(float)) {
                slot_ids.push_back(sqlite3_column_int64(scan_stmt, 0));
                embeddings.emplace_back(blob, blob + 512);
            }
        }
        if (rc != SQLITE_DONE) LOGE("merge_similar_entities: failed to fetch entities: %s", sqlite3_errmsg(db));
        sqlite3_finalize(scan_stmt);
        if (rc != SQLITE_DONE) return;

        near.resize(slot_ids.size());
        size_t pairs = 0;
        for (size_t i = 0; i < slot_ids.size(); ++i) {
            for (size_t j = i + 1; j < slot_ids.size(); ++j) {
                if (l2_512(embeddings[i], embeddings[j]) <= MERGE_THRESHOLD) {
                    near[i].push_back(j);
                    ++pairs;
                }
            }
        }
        if (pairs == 0) {
            LOGI("Entity merge pass completed: 0 merges performed.");
            return;
        }
    }

    // IMMEDIATE takes the write lock before anything is re-read, so names and detections cannot
    // change under the merges. Any failure below rolls the whole pass back.
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("merge_similar_entities failed to begin transaction: %s", sqlite3_errmsg(db));
        return;
    }

    // An entity's current centroid, sample count and user metadata
    struct EntityCentroid {
        int64_t entity_id;
        int sample_count;
        std::vector<float> embedding;
        std::string name, relation, location;  // empty = not set by the user
        int age = 0;                           // 0 = not set
        bool has_metadata() const { return !name.empty() || !relation.empty() || !location.empty() || age > 0; }
    };

    sqlite3_stmt* row_stmt = prepare_or_log(db, R"(
        SELECT e.sample_count, pc.embedding, e.display_name, e.relation, e.age, e.location
        FROM person_centroids pc
        JOIN entities e ON e.entity_id = pc.entity_id
        WHERE pc.entity_id = ?
    )");
    sqlite3_stmt* colink_stmt = prepare_or_log(db, "SELECT 1 FROM face_detections a JOIN face_detections b ON a.file_id = b.file_id WHERE a.entity_id = ? AND b.entity_id = ? LIMIT 1");
    sqlite3_stmt* move_det_stmt = prepare_or_log(db, "UPDATE face_detections SET entity_id = ? WHERE entity_id = ?");
    // Carries the score; a file both entities already have keeps the higher one.
    sqlite3_stmt* move_mem_stmt = prepare_or_log(db, "INSERT INTO entity_memberships (entity_id, file_id, score, created_at) SELECT ?, file_id, score, created_at FROM entity_memberships WHERE entity_id = ? ON CONFLICT(entity_id, file_id) DO UPDATE SET score = MAX(score, excluded.score)");
    sqlite3_stmt* del_mem_stmt = prepare_or_log(db, "DELETE FROM entity_memberships WHERE entity_id = ?");
    sqlite3_stmt* upd_cent_stmt = prepare_or_log(db, "UPDATE person_centroids SET embedding = ? WHERE entity_id = ?");
    // ?1 = entity being deleted: its metadata fills whatever the keeper has not set.
    sqlite3_stmt* upd_ent_stmt = prepare_or_log(db, R"(
        UPDATE entities SET
            display_name = COALESCE(NULLIF(display_name, ''), (SELECT display_name FROM entities WHERE entity_id = ?1)),
            relation = COALESCE(NULLIF(relation, ''), (SELECT relation FROM entities WHERE entity_id = ?1)),
            age = COALESCE(age, (SELECT age FROM entities WHERE entity_id = ?1)),
            location = COALESCE(NULLIF(location, ''), (SELECT location FROM entities WHERE entity_id = ?1)),
            sample_count = ?2, updated_at = ?3
        WHERE entity_id = ?4
    )");
    sqlite3_stmt* del_cent_stmt = prepare_or_log(db, "DELETE FROM person_centroids WHERE entity_id = ?");
    sqlite3_stmt* del_ent_stmt = prepare_or_log(db, "DELETE FROM entities WHERE entity_id = ?");

    bool ok = row_stmt && colink_stmt && move_det_stmt && move_mem_stmt && del_mem_stmt &&
              upd_cent_stmt && upd_ent_stmt && del_cent_stmt && del_ent_stmt;

    // Reads an entity's current row (it reflects the merges already made in this pass).
    // Returns SQLITE_ROW, SQLITE_DONE if the entity is gone, or an error code.
    auto read_entity = [&](int64_t entity_id, EntityCentroid& ec) {
        sqlite3_bind_int64(row_stmt, 1, entity_id);
        int rc = sqlite3_step(row_stmt);
        if (rc == SQLITE_ROW) {
            auto text = [&](int col) {
                const unsigned char* t = sqlite3_column_text(row_stmt, col);
                return std::string(t ? reinterpret_cast<const char*>(t) : "");
            };
            const float* blob = static_cast<const float*>(sqlite3_column_blob(row_stmt, 1));
            if (blob && sqlite3_column_bytes(row_stmt, 1) == 512 * sizeof(float)) {
                ec.entity_id = entity_id;
                ec.sample_count = sqlite3_column_int(row_stmt, 0);
                ec.embedding.assign(blob, blob + 512);
                ec.name = text(2);
                ec.relation = text(3);
                ec.age = sqlite3_column_int(row_stmt, 4);  // NULL reads as 0
                ec.location = text(5);
            } else {
                rc = SQLITE_DONE;  // no usable centroid: never merged, as in the scan
            }
        } else if (rc != SQLITE_DONE) {
            LOGE("merge_similar_entities: failed to read entity %lld: %s", (long long)entity_id, sqlite3_errmsg(db));
        }
        sqlite3_reset(row_stmt);
        sqlite3_clear_bindings(row_stmt);
        return rc;
    };

    // Both set and different: the user says these are two people, and a merge would lose one value.
    auto conflict = [](const std::string& x, const std::string& y) { return !x.empty() && !y.empty() && x != y; };

    // Greedy merge in slot order (larger entities first), each slot against its candidates in order.
    // Every candidate is re-checked on the current rows: the keeper's centroid moves with each merge
    // and the user may have edited a name since the scan. A merge adds the merged entity's candidates
    // to the keeper's. Unlike the old single-phase pass, a keeper is compared only with candidates
    // found in the lock-free scan, so a pair that comes within the threshold only through a merge in
    // this pass is found by the next pass (one pass merges slightly less than before).
    int merge_count = 0;
    std::vector<bool> merged(slot_ids.size(), false);

    for (size_t i = 0; ok && i < slot_ids.size(); ++i) {
        if (merged[i]) continue;

        std::set<size_t> todo(near[i].begin(), near[i].end());  // ascending: slots keep the greedy order
        while (!todo.empty()) {
            size_t j = *todo.begin();
            todo.erase(todo.begin());
            if (merged[j]) continue;

            // a ends up the keeper, b the entity merged into it
            EntityCentroid a, b;
            int rc_a = read_entity(slot_ids[i], a);
            int rc_b = rc_a == SQLITE_ROW ? read_entity(slot_ids[j], b) : SQLITE_DONE;
            if ((rc_a != SQLITE_ROW && rc_a != SQLITE_DONE) || (rc_b != SQLITE_ROW && rc_b != SQLITE_DONE)) {
                ok = false;
                break;
            }
            if (rc_b != SQLITE_ROW) continue;  // one of the two is gone

            float dist = l2_512(a.embedding, b.embedding);
            if (dist > MERGE_THRESHOLD) continue;

            if (conflict(a.name, b.name) || conflict(a.relation, b.relation) || conflict(a.location, b.location) ||
                (a.age > 0 && b.age > 0 && a.age != b.age)) {
                continue;
            }

            // Cannot-link: two entities seen in the same photo are different people.
            sqlite3_bind_int64(colink_stmt, 1, a.entity_id);
            sqlite3_bind_int64(colink_stmt, 2, b.entity_id);
            int rc = sqlite3_step(colink_stmt);
            if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
                LOGE("merge_similar_entities: co-occurrence check failed: %s", sqlite3_errmsg(db));
                ok = false;
            }
            sqlite3_reset(colink_stmt);
            sqlite3_clear_bindings(colink_stmt);
            if (!ok) break;
            if (rc == SQLITE_ROW) continue;

            // Keeper: the protected entity, else the one carrying user metadata, else the larger one (i).
            // Swapping the two slots keeps the keeper at i, so the loop goes on comparing it with the rest.
            if (b.entity_id == protected_entity_id ||
                (a.entity_id != protected_entity_id && !a.has_metadata() && b.has_metadata())) {
                std::swap(a, b);
                std::swap(slot_ids[i], slot_ids[j]);
            }

            LOGI("Merging entity %lld into %lld (distance=%.4f)",
                 (long long)b.entity_id, (long long)a.entity_id, dist);

            // Keeper centroid becomes the sample-weighted average
            int new_count = a.sample_count + b.sample_count;
            float w_a = new_count > 0 ? (float)a.sample_count / (float)new_count : 0.5f;
            float w_b = 1.0f - w_a;
            std::vector<float> new_centroid(512);
            float norm = 0.0f;
            for (int d = 0; d < 512; ++d) {
                new_centroid[d] = a.embedding[d] * w_a + b.embedding[d] * w_b;
                norm += new_centroid[d] * new_centroid[d];
            }
            norm = std::sqrt(norm);
            if (norm > 0) {
                for (int d = 0; d < 512; ++d) new_centroid[d] /= norm;
            }

            sqlite3_bind_int64(move_det_stmt, 1, a.entity_id);
            sqlite3_bind_int64(move_det_stmt, 2, b.entity_id);
            ok = step_done(db, move_det_stmt, "merge move detections");

            sqlite3_bind_int64(move_mem_stmt, 1, a.entity_id);
            sqlite3_bind_int64(move_mem_stmt, 2, b.entity_id);
            ok = ok && step_done(db, move_mem_stmt, "merge move memberships");

            sqlite3_bind_int64(del_mem_stmt, 1, b.entity_id);
            ok = ok && step_done(db, del_mem_stmt, "merge delete memberships");

            sqlite3_bind_blob(upd_cent_stmt, 1, new_centroid.data(), 512 * sizeof(float), SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd_cent_stmt, 2, a.entity_id);
            ok = ok && step_done(db, upd_cent_stmt, "merge update centroid");

            sqlite3_bind_int64(upd_ent_stmt, 1, b.entity_id);
            sqlite3_bind_int(upd_ent_stmt, 2, new_count);
            sqlite3_bind_int64(upd_ent_stmt, 3, static_cast<int64_t>(std::time(nullptr)));
            sqlite3_bind_int64(upd_ent_stmt, 4, a.entity_id);
            ok = ok && step_done(db, upd_ent_stmt, "merge update keeper");

            sqlite3_bind_int64(del_cent_stmt, 1, b.entity_id);
            ok = ok && step_done(db, del_cent_stmt, "merge delete centroid");

            sqlite3_bind_int64(del_ent_stmt, 1, b.entity_id);
            ok = ok && step_done(db, del_ent_stmt, "merge delete entity");

            if (!ok) break;

            merged[j] = true;
            merge_count++;
            todo.insert(near[j].begin(), near[j].end());  // all later than j
        }
    }

    sqlite3_finalize(row_stmt);
    sqlite3_finalize(colink_stmt);
    sqlite3_finalize(move_det_stmt);
    sqlite3_finalize(move_mem_stmt);
    sqlite3_finalize(del_mem_stmt);
    sqlite3_finalize(upd_cent_stmt);
    sqlite3_finalize(upd_ent_stmt);
    sqlite3_finalize(del_cent_stmt);
    sqlite3_finalize(del_ent_stmt);

    if (ok && sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE("merge_similar_entities failed to commit: %s", sqlite3_errmsg(db));
        ok = false;
    }
    if (!ok) {
        LOGE("merge_similar_entities rolled back; no entities merged");
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    LOGI("Entity merge pass completed: %d merges performed.", merge_count);
}

bool reset_face_data(sqlite3* db) {
    if (!db) return false;

    LOGI("Resetting all face data for re-indexing...");

    // One exec: it stops at the first failing statement, and the rollback then leaves nothing half-reset.
    // Deleting entities also clears entity_relations / entity_relation_files through their cascades.
    const char* sql =
        "BEGIN TRANSACTION;"
        "DELETE FROM entity_memberships;"
        "DELETE FROM face_vec;"
        "DELETE FROM face_detections;"
        "DELETE FROM person_centroids;"
        "DELETE FROM entities;"
        "UPDATE files SET face_count = 0;"  // NOT NULL column; the face pipeline rewrites it per file
        "COMMIT;";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("reset_face_data failed: %s", errMsg);
        sqlite3_free(errMsg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    LOGI("Face data reset complete.");
    return true;
}
