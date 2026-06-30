#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "sqlite3.h"
#include "Preprocessing/Segregation/Seg_model.hpp"
 
bool init_face_schema(sqlite3* db);
bool save_face_detection(sqlite3* db, int64_t file_id, const FaceResult& face, int64_t* out_detection_id);
bool save_face_embedding(sqlite3* db, int64_t detection_id, const std::vector<float>& embedding);
bool run_face_pipeline(sqlite3* db, const std::string& abs_path, int64_t file_id, Face_embedding& face_model);
bool cluster_faces_for_file(sqlite3* db, int64_t file_id);
void recluster_pending_faces(sqlite3* db);
 
// Maintenance pass: finds entities whose centroids are nearly identical
// (i.e. almost certainly the same real person, split across two entities
// because earlier blurry/low-quality faces failed to match) and merges the
// smaller one into the larger one. Safe to run periodically, e.g. after a
// batch of indexing finishes. Returns the number of entities merged away.
int merge_duplicate_entities(sqlite3* db, float merge_distance_threshold = 0.4f);
 