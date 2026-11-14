#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"

struct search_result {
    int64_t file_id;
    std::string abs_path;
    std::string display_name;
    int64_t size_bytes;
    int64_t mtime_unix;
    float distance;
};

#include "CLIP_model.hpp"
#include "typo_rectifyier.hpp"

// Forward declarations
class CLIPTokenizer;
namespace vanta { namespace ner { class NERModel; } }
struct EntityCandidate;

// Initializes the query engine, loading any necessary text models.
bool init_query_engine(const std::string& db_path = "");

// Sets optional search feature toggles globally
void set_query_options(bool use_graph, bool use_spellcheck, bool use_intent);

// Rebuilds the query engine dictionary dynamically from the database.
void rebuild_query_engine_dictionary(const std::string& db_path);

// Corrects the query using the typo rectifier. When a loaded tokenizer is given,
// words present in its vocabulary are treated as correctly spelled.
std::string get_corrected_query(const std::string& raw_query, const std::string& db_path = "",
                                const CLIPTokenizer* tokenizer = nullptr);

// Integer Damerau-Levenshtein distance between two strings.
int damerau_levenshtein(const std::string& s1, const std::string& s2);

// Resolves a span text (e.g. "dad", "my sister") to an entity ID by fuzzy
// matching against display_name and relation of all person entities.
// The allowed edit distance scales with the span length: exact match for up to
// 4 characters, 1 for 5-7, 2 for 8 or more. Entities with neither a name nor a
// relation are skipped. Returns -1 if nothing matches within that budget.
int64_t resolve_span_to_entity_id(
    const std::string& span_text,
    const std::vector<EntityCandidate>& candidates,
    int64_t owner_entity_id);

// Given a raw string query and database path, returns the top K similar images.
// When ner_model is non-null and loaded, uses NER span resolution + entity
// graph routing before CLIP re-ranking.
std::vector<search_result> search_images(
    const std::string& db_path,
    const std::string& raw_query,
    CLIP_session* clip_session,
    CLIPTokenizer* tokenizer,
    vanta::ner::NERModel* ner_model = nullptr,
    int64_t owner_entity_id = -1,
    int top_k = 30);

