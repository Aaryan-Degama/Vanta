#ifndef VANTA_GRAPH_DB_HPP
#define VANTA_GRAPH_DB_HPP

#include <sqlite3.h>
#include <string>
#include <vector>

struct EntityMeta {
    int64_t entity_id;
    std::string display_name;
    int sample_count;
    float confidence;
};

struct FaceCrop {
    int64_t file_id;
    std::string abs_path;
    int bbox_x;
    int bbox_y;
    int bbox_w;
    int bbox_h;
};

struct NeighborResult {
    int64_t neighbor_id;
    std::string display_name;
    int co_occurrence_count;
};

struct EntityFile {
    int64_t file_id;
    std::string abs_path;
};

bool init_graph_schema(sqlite3* db);
std::vector<EntityMeta> get_top_entities(sqlite3* db, int limit);
FaceCrop get_best_face_crop(sqlite3* db, int64_t entity_id);
bool set_entity_name(sqlite3* db, int64_t entity_id, const std::string& name);
bool update_graph_for_file(sqlite3* db, int64_t file_id);
std::vector<NeighborResult> get_neighbors(sqlite3* db, int64_t entity_id, int limit);
std::vector<EntityFile> get_entity_files(sqlite3* db, int64_t entity_id, int limit);

#endif // VANTA_GRAPH_DB_HPP
