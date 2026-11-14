#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "sqlite3.h"
#include "Seg_model.hpp"

bool init_face_schema(sqlite3* db);
bool save_face_detection(sqlite3* db, int64_t file_id, const FaceResult& face, const std::string& aligned_crop_path, int64_t* out_detection_id);
bool save_face_embedding(sqlite3* db, int64_t detection_id, const std::vector<float>& embedding);
// A face ready to be stored: the detection with its embedding in face.embedding, and its crop file ("" = none).
struct ComputedFace {
    FaceResult face;
    std::string crop_path;
};
// Model step, no database access: detects and embeds the faces of one photo and writes their crops.
// Faces without an embedding are left out. Returns false if the image cannot be read.
bool compute_faces(const std::string& abs_path, int64_t file_id, Face_embedding& face_model, std::vector<ComputedFace>& out);
// Store step: saves the faces and sets files.face_count to the number stored. Runs inside the photo's transaction.
void store_faces(sqlite3* db, int64_t file_id, const std::vector<ComputedFace>& faces);
bool cluster_faces_for_file(sqlite3* db, int64_t file_id);
void recluster_pending_faces(sqlite3* db);
// protected_entity_id (e.g. the owner's "me" entity) is never deleted by a merge; -1 = none.
void merge_similar_entities(sqlite3* db, int64_t protected_entity_id = -1);
bool reset_face_data(sqlite3* db);
