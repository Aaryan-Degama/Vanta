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

/**
 * Full metadata for an entity, including the new relation/age/location
 * columns added by the NER pipeline migration.
 */
struct EntityMetadata {
    int64_t     entity_id;
    std::string display_name;
    std::string relation;
    int         age;           // 0 = not set
    std::string location;
    int         sample_count;
    float       confidence;
};

/**
 * Lightweight entity record used by resolve_span_to_entity_id() at
 * search time. Only the fields needed for fuzzy matching are included.
 */
struct EntityCandidate {
    int64_t     entity_id;
    std::string display_name;
    std::string relation;
    int         sample_count;
};

struct FaceCrop {
    int64_t file_id;
    std::string abs_path;
    int bbox_x;
    int bbox_y;
    int bbox_w;
    int bbox_h;
    std::string aligned_crop_path;
    float det_score;
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

// ── NER pipeline additions ──

/** Sets display_name, relation, age, location on an entity. */
bool set_entity_metadata(sqlite3* db, int64_t entity_id,
                         const std::string& name,
                         const std::string& relation,
                         int age,
                         const std::string& location);

/** Returns full metadata for a single entity. */
EntityMetadata get_entity_metadata(sqlite3* db, int64_t entity_id);

/** Returns all person entities (for NER span resolution at search time). */
std::vector<EntityCandidate> get_all_person_entities(sqlite3* db);

#endif // VANTA_GRAPH_DB_HPP
