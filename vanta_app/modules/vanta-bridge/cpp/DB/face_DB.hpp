#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "sqlite3.h"
#include "Seg_model.hpp"

bool init_face_schema(sqlite3* db);
bool save_face_detection(sqlite3* db, int64_t file_id, const FaceResult& face, const std::string& aligned_crop_path, int64_t* out_detection_id);
bool save_face_embedding(sqlite3* db, int64_t detection_id, const std::vector<float>& embedding);
bool run_face_pipeline(sqlite3* db, const std::string& abs_path, int64_t file_id, Face_embedding& face_model);
bool cluster_faces_for_file(sqlite3* db, int64_t file_id);
void recluster_pending_faces(sqlite3* db);
void merge_similar_entities(sqlite3* db);
bool reset_face_data(sqlite3* db);
