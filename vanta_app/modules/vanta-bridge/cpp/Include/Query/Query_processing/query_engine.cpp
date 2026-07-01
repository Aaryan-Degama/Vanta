#include "query_engine.hpp"
#include "CLIP_tokenizer.hpp"
#include "ner.hpp"
#include "graph_db.hpp"
#include "IntentBuilder.hpp"
#include <iostream>
#include <android/log.h>
#define SQLITE_CORE 1
#include "sqlite-vec.h"
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#define LOG_TAG "VantaQueryEngine"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static VantaProductionAnalyzer* g_analyzer = nullptr;
static vanta::query::IntentBuilder* g_intent_builder = nullptr;

// ── Helper: lowercase ──
static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return out;
}

bool init_query_engine(const std::string& db_path) {
    // In the future, this will initialize the text tokenizer and text ONNX model.
    if (!g_analyzer) {
        SymSpellDictionary dict;
        std::vector<std::string> whitelist;

        // Add static relation and place/event terms matching IntentBuilder
        std::vector<std::string> static_terms = {
            "me", "myself", "self", "family", "group", "people",
            "father", "dad", "mother", "mom", "brother", "sister", 
            "friend", "friends", "wife", "husband", "uncle", "aunt", "cousin",
            "beach", "mountain", "mountains", "kedarnath", "temple", 
            "office", "school", "home", "park", "restaurant",
            "graduation", "birthday", "wedding", "diwali", "holi", 
            "festival", "vacation", "trip"
        };
        for (const auto& term : static_terms) {
            whitelist.push_back(to_lower(term));
        }

        if (!db_path.empty()) {
            sqlite3* db = nullptr;
            sqlite3_auto_extension((void (*)())sqlite3_vec_init);
            if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                // Person names
                const char* person_sql = "SELECT display_name FROM entities WHERE entity_type = 'person' AND display_name IS NOT NULL AND display_name != ''";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, person_sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        if (text) {
                            std::string dn(text);
                            std::istringstream iss(dn);
                            std::string token;
                            while (iss >> token) {
                                whitelist.push_back(to_lower(token));
                            }
                        }
                    }
                    sqlite3_finalize(stmt);
                }

                // Relation terms
                const char* rel_sql = "SELECT DISTINCT relation FROM entities WHERE relation IS NOT NULL AND relation != ''";
                if (sqlite3_prepare_v2(db, rel_sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        if (text) {
                            std::string rel(text);
                            std::istringstream iss(rel);
                            std::string token;
                            while (iss >> token) {
                                whitelist.push_back(to_lower(token));
                            }
                        }
                    }
                    sqlite3_finalize(stmt);
                }

                sqlite3_close(db);
            }
        }

        g_analyzer = new VantaProductionAnalyzer(dict, whitelist);
    }
    if (!g_intent_builder) {
        g_intent_builder = new vanta::query::IntentBuilder();
    }
    LOGI("Query engine initialized.");
    return true;
}

void rebuild_query_engine_dictionary(const std::string& db_path) {
    if (g_analyzer) {
        delete g_analyzer;
        g_analyzer = nullptr;
    }
    init_query_engine(db_path);
    LOGI("Query engine dictionary rebuilt.");
}

std::string get_corrected_query(const std::string& raw_query, const std::string& db_path) {
    if (!g_analyzer) {
        init_query_engine(db_path);
    }
    QueryAnalysis analysis = g_analyzer->analyze(raw_query);
    if (analysis.was_corrected) {
        LOGI("Query corrected: %s -> %s", raw_query.c_str(), analysis.corrected_query.c_str());
    }
    return analysis.corrected_query;
}


// ── Damerau-Levenshtein distance (integer) ──
int damerau_levenshtein(const std::string& s1, const std::string& s2) {
    int len1 = static_cast<int>(s1.size());
    int len2 = static_cast<int>(s2.size());

    // Quick exit for empty strings
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    // 1D vector mapped as 2D: (len1+1) x (len2+1)
    std::vector<int> d((len1 + 1) * (len2 + 1));
    auto at = [&](int i, int j) -> int& { return d[i * (len2 + 1) + j]; };

    for (int i = 0; i <= len1; ++i) at(i, 0) = i;
    for (int j = 0; j <= len2; ++j) at(0, j) = j;

    for (int i = 1; i <= len1; ++i) {
        for (int j = 1; j <= len2; ++j) {
            int cost = (std::tolower(s1[i - 1]) == std::tolower(s2[j - 1])) ? 0 : 1;
            at(i, j) = std::min({
                at(i - 1, j) + 1,          // deletion
                at(i, j - 1) + 1,          // insertion
                at(i - 1, j - 1) + cost    // substitution
            });
            // Transposition
            if (i > 1 && j > 1 &&
                std::tolower(s1[i - 1]) == std::tolower(s2[j - 2]) &&
                std::tolower(s1[i - 2]) == std::tolower(s2[j - 1])) {
                at(i, j) = std::min(at(i, j), at(i - 2, j - 2) + cost);
            }
        }
    }

    return at(len1, len2);
}

// ── Span resolution ──
int64_t resolve_span_to_entity_id(
    const std::string& span_text,
    const std::vector<EntityCandidate>& candidates,
    int64_t /*owner_entity_id*/) {

    // Strip leading "my " (case-insensitive)
    std::string lower = to_lower(span_text);
    if (lower.size() > 3 && lower.substr(0, 3) == "my ") {
        lower = lower.substr(3);
    }

    int best_dist = 3;   // reject anything above 2
    int64_t best_id = -1;
    int best_count = 0;

    for (const auto& c : candidates) {
        // Check against display_name AND relation
        std::string dn = to_lower(c.display_name);
        std::string rel = to_lower(c.relation);

        int d = std::min(
            damerau_levenshtein(lower, dn),
            rel.empty() ? 999 : damerau_levenshtein(lower, rel)
        );

        // Prefer lower dist; tie-break on higher sample_count
        if (d < best_dist || (d == best_dist && c.sample_count > best_count)) {
            best_dist = d;
            best_id = c.entity_id;
            best_count = c.sample_count;
        }
    }

    if (best_dist <= 2) {
        LOGI("Resolved span '%s' → entity_id=%ld (dist=%d)", span_text.c_str(), (long)best_id, best_dist);
    }

    return best_dist <= 2 ? best_id : -1;
}

// ── Dot product (cosine similarity for normalized vectors) ──
static float dot_product(const float* a, const float* b, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

// ── Main search pipeline ──
std::vector<search_result> search_images(
    const std::string& db_path,
    const std::string& raw_query,
    CLIP_session* clip_session,
    CLIPTokenizer* tokenizer,
    vanta::ner::NERModel* ner_model,
    int64_t owner_entity_id,
    int top_k) {

    std::vector<search_result> results;

    if (!clip_session || !clip_session->is_loaded()) {
        LOGE("CLIP session not loaded for text search.");
        return results;
    }

    if (!tokenizer || !tokenizer->is_loaded()) {
        LOGE("CLIP tokenizer not loaded for text search.");
        return results;
    }

    // Ensure intent builder is initialized
    if (!g_intent_builder) {
        g_intent_builder = new vanta::query::IntentBuilder();
    }

    // ── Step 1: Typo-correct ──
    std::string corrected_query = get_corrected_query(raw_query, db_path);
    LOGI("search_images: raw='%s' corrected='%s'", raw_query.c_str(), corrected_query.c_str());

    // ── Step 2: Run NER ──
    std::vector<vanta::ner::NERSpan> spans;
    if (ner_model && ner_model->is_loaded()) {
        spans = ner_model->run(corrected_query);
        LOGI("NER returned %zu spans", spans.size());
        for (const auto& sp : spans) {
            LOGI("  span: label='%s' text='%s'", sp.label.c_str(), sp.text.c_str());
        }
    }

    // ── Step 3: Resolve spans ──
    std::vector<int64_t> resolved_entity_ids;
    std::vector<std::string> unresolved_words;

    // Track which word positions in the corrected query are covered by resolved spans
    // We'll use a simpler approach: track the text of resolved spans
    std::unordered_set<std::string> resolved_span_texts;

    if (!spans.empty()) {
        // Open DB to get person entities for resolution
        sqlite3* resolve_db = nullptr;
        sqlite3_auto_extension((void (*)())sqlite3_vec_init);
        if (sqlite3_open_v2(db_path.c_str(), &resolve_db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            std::vector<EntityCandidate> candidates = get_all_person_entities(resolve_db);
            sqlite3_close(resolve_db);

            for (const auto& span : spans) {
                if (span.label == "SELF") {
                    if (owner_entity_id != -1) {
                        resolved_entity_ids.push_back(owner_entity_id);
                        resolved_span_texts.insert(to_lower(span.text));
                        LOGI("SELF span '%s' → owner entity %ld", span.text.c_str(), (long)owner_entity_id);
                    } else {
                        LOGI("SELF span '%s' but no owner set, keeping for CLIP", span.text.c_str());
                        unresolved_words.push_back(span.text);
                    }
                } else if (span.label == "PERSON") {
                    int64_t eid = resolve_span_to_entity_id(span.text, candidates, owner_entity_id);
                    if (eid != -1) {
                        resolved_entity_ids.push_back(eid);
                        resolved_span_texts.insert(to_lower(span.text));
                    } else {
                        LOGI("PERSON span '%s' unresolved, keeping for CLIP", span.text.c_str());
                        unresolved_words.push_back(span.text);
                    }
                }
                // RELATION spans: ignored for entity resolution (just connectors)
            }
        }

        // Deduplicate entity IDs
        std::sort(resolved_entity_ids.begin(), resolved_entity_ids.end());
        resolved_entity_ids.erase(
            std::unique(resolved_entity_ids.begin(), resolved_entity_ids.end()),
            resolved_entity_ids.end());
        // Remove -1s if any snuck in
        resolved_entity_ids.erase(
            std::remove(resolved_entity_ids.begin(), resolved_entity_ids.end(), -1),
            resolved_entity_ids.end());

        LOGI("Resolved %zu entity IDs, %zu unresolved words",
             resolved_entity_ids.size(), unresolved_words.size());
    }

    // ── Step 4: Build CLIP caption ──
    std::string clip_caption;
    {
        // Take corrected_query words NOT covered by resolved spans
        std::string cleaned;
        std::istringstream iss(corrected_query);
        std::string word;
        while (iss >> word) {
            std::string lw = to_lower(word);
            // Check if this word is part of any resolved span text
            bool covered = false;
            for (const auto& st : resolved_span_texts) {
                if (st.find(lw) != std::string::npos) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                if (!cleaned.empty()) cleaned += " ";
                cleaned += word;
            }
        }

        // Append unresolved words
        for (const auto& uw : unresolved_words) {
            if (!cleaned.empty()) cleaned += " ";
            cleaned += uw;
        }

        // Pass through IntentBuilder for CLIP-friendly description
        if (!cleaned.empty()) {
            clip_caption = g_intent_builder->buildIntent("", cleaned);
            if (clip_caption.empty()) {
                // IntentBuilder filtered everything? Use the cleaned text directly
                clip_caption = cleaned;
            }
        }

        LOGI("CLIP caption: '%s'", clip_caption.c_str());
    }

    // ── Step 5: Build candidate file_id set ──
    std::vector<int64_t> candidate_file_ids;
    bool has_candidates = false;

    if (!resolved_entity_ids.empty()) {
        sqlite3* cand_db = nullptr;
        sqlite3_auto_extension((void (*)())sqlite3_vec_init);
        if (sqlite3_open_v2(db_path.c_str(), &cand_db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {

            if (resolved_entity_ids.size() == 1) {
                // Single entity: SELECT file_ids directly
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT file_id FROM entity_memberships WHERE entity_id = ?";
                if (sqlite3_prepare_v2(cand_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, resolved_entity_ids[0]);
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        candidate_file_ids.push_back(sqlite3_column_int64(stmt, 0));
                    }
                    sqlite3_finalize(stmt);
                }
            } else if (resolved_entity_ids.size() == 2) {
                // Exactly two entities: try to use the Knowledge Graph (entity_relation_files) for an optimized inner join
                int64_t a = std::min(resolved_entity_ids[0], resolved_entity_ids[1]);
                int64_t b = std::max(resolved_entity_ids[0], resolved_entity_ids[1]);
                
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT file_id FROM entity_relation_files WHERE entity_a = ? AND entity_b = ?";
                if (sqlite3_prepare_v2(cand_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, a);
                    sqlite3_bind_int64(stmt, 2, b);
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        candidate_file_ids.push_back(sqlite3_column_int64(stmt, 0));
                    }
                    sqlite3_finalize(stmt);
                }
            }

            if (candidate_file_ids.empty() && resolved_entity_ids.size() >= 2) {
                // Multiple entities (>2) OR graph intersect was empty: INTERSECT in C++
                // Load each entity's file_id set, keep only IDs present in ALL sets
                std::vector<std::unordered_set<int64_t>> sets;
                for (int64_t eid : resolved_entity_ids) {
                    std::unordered_set<int64_t> fids;
                    sqlite3_stmt* stmt = nullptr;
                    const char* sql = "SELECT file_id FROM entity_memberships WHERE entity_id = ?";
                    if (sqlite3_prepare_v2(cand_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_int64(stmt, 1, eid);
                        while (sqlite3_step(stmt) == SQLITE_ROW) {
                            fids.insert(sqlite3_column_int64(stmt, 0));
                        }
                        sqlite3_finalize(stmt);
                    }
                    sets.push_back(std::move(fids));
                }

                if (!sets.empty()) {
                    // Start with the smallest set for efficiency
                    size_t smallest_idx = 0;
                    for (size_t i = 1; i < sets.size(); ++i) {
                        if (sets[i].size() < sets[smallest_idx].size()) {
                            smallest_idx = i;
                        }
                    }

                    for (int64_t fid : sets[smallest_idx]) {
                        bool in_all = true;
                        for (size_t i = 0; i < sets.size(); ++i) {
                            if (i == smallest_idx) continue;
                            if (sets[i].find(fid) == sets[i].end()) {
                                in_all = false;
                                break;
                            }
                        }
                        if (in_all) {
                            candidate_file_ids.push_back(fid);
                        }
                    }

                    if (candidate_file_ids.empty()) {
                        LOGI("Intersect empty. Returning 0 results instead of falling back to UNION.");
                    }
                }
            }

            sqlite3_close(cand_db);
        }

        has_candidates = !candidate_file_ids.empty();
        LOGI("Candidate set: %zu file_ids", candidate_file_ids.size());

        // If we resolved entities but found NO intersecting files, 
        // we should return 0 results rather than falling back to global CLIP search.
        if (!has_candidates && !resolved_entity_ids.empty()) {
            LOGI("Resolved %zu entities, but intersection is empty. Returning 0 results.", resolved_entity_ids.size());
            return results;
        }
    }

    // Open DB for the final query stage
    sqlite3* db = nullptr;
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for search: %s", sqlite3_errmsg(db));
        return results;
    }

    // ── Step 6a: Candidates exist → restricted search ──
    if (has_candidates) {

        if (!clip_caption.empty()) {
            // Get query text embedding
            std::vector<int64_t> tokens = tokenizer->encode(clip_caption);
            std::vector<float> query_embedding;
            try {
                query_embedding = clip_session->get_text_embedding(tokens);
            } catch (const std::exception& e) {
                LOGE("Failed to get text embedding: %s", e.what());
                sqlite3_close(db);
                return results;
            }

            int embed_dim = static_cast<int>(query_embedding.size());

            // Fetch CLIP embeddings for candidate file_ids using rowid point lookups
            struct scored_candidate {
                int64_t file_id;
                float score;
            };
            std::vector<scored_candidate> scored;

            // Build a batched query with placeholders for each candidate
            // For simplicity and because sqlite-vec rowid lookups work with
            // individual binds, we query one by one but reuse the statement.
            {
                const char* emb_sql = "SELECT file_id, embedding FROM clip_vec WHERE rowid = ?";
                sqlite3_stmt* emb_stmt = nullptr;
                if (sqlite3_prepare_v2(db, emb_sql, -1, &emb_stmt, nullptr) == SQLITE_OK) {
                    for (int64_t fid : candidate_file_ids) {
                        sqlite3_reset(emb_stmt);
                        sqlite3_bind_int64(emb_stmt, 1, fid);

                        if (sqlite3_step(emb_stmt) == SQLITE_ROW) {
                            int64_t rid = sqlite3_column_int64(emb_stmt, 0);
                            const float* emb_data = reinterpret_cast<const float*>(
                                sqlite3_column_blob(emb_stmt, 1));
                            int emb_bytes = sqlite3_column_bytes(emb_stmt, 1);
                            int emb_count = emb_bytes / static_cast<int>(sizeof(float));

                            if (emb_data && emb_count == embed_dim) {
                                float sim = dot_product(query_embedding.data(), emb_data, embed_dim);
                                scored.push_back({rid, sim});
                            }
                        }
                    }
                    sqlite3_finalize(emb_stmt);
                }
            }

            // Sort descending by similarity, take top_k
            std::sort(scored.begin(), scored.end(),
                [](const scored_candidate& a, const scored_candidate& b) {
                    return a.score > b.score;
                });

            if (static_cast<int>(scored.size()) > top_k) {
                scored.resize(top_k);
            }

            // Look up file details
            const char* file_sql = "SELECT abs_path, display_name, size_bytes, mtime_unix FROM files WHERE id = ?";
            sqlite3_stmt* file_stmt = nullptr;
            if (sqlite3_prepare_v2(db, file_sql, -1, &file_stmt, nullptr) == SQLITE_OK) {
                for (const auto& sc : scored) {
                    sqlite3_reset(file_stmt);
                    sqlite3_bind_int64(file_stmt, 1, sc.file_id);

                    if (sqlite3_step(file_stmt) == SQLITE_ROW) {
                        search_result res;
                        res.file_id = sc.file_id;
                        res.distance = 1.0f - sc.score; // convert similarity to distance

                        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 0));
                        res.abs_path = path ? path : "";

                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 1));
                        res.display_name = name ? name : "";

                        res.size_bytes = sqlite3_column_int64(file_stmt, 2);
                        res.mtime_unix = sqlite3_column_int64(file_stmt, 3);

                        // Time-based Burst Shot Filter (NMS)
                        // If this photo was taken within 30 seconds of an already accepted, higher-scoring photo, skip it to reduce noise.
                        bool is_burst = false;
                        if (res.mtime_unix > 0) {
                            for (const auto& accepted : results) {
                                if (accepted.mtime_unix > 0 && std::abs(accepted.mtime_unix - res.mtime_unix) < 30) {
                                    is_burst = true;
                                    break;
                                }
                            }
                        }

                        if (!is_burst) {
                            results.push_back(res);
                        }
                    }
                }
                sqlite3_finalize(file_stmt);
            }

        } else {
            // No CLIP caption → sort candidates by entity_memberships score
            LOGI("No CLIP caption, sorting by entity membership score");

            // Get scores for the first resolved entity (primary sort key)
            struct membership_hit {
                int64_t file_id;
                float score;
            };
            std::vector<membership_hit> hits;

            if (!resolved_entity_ids.empty()) {
                const char* mem_sql = "SELECT file_id, score FROM entity_memberships WHERE entity_id = ? ORDER BY score DESC LIMIT ?";
                sqlite3_stmt* mem_stmt = nullptr;
                if (sqlite3_prepare_v2(db, mem_sql, -1, &mem_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(mem_stmt, 1, resolved_entity_ids[0]);
                    sqlite3_bind_int(mem_stmt, 2, top_k);
                    while (sqlite3_step(mem_stmt) == SQLITE_ROW) {
                        membership_hit h;
                        h.file_id = sqlite3_column_int64(mem_stmt, 0);
                        h.score = static_cast<float>(sqlite3_column_double(mem_stmt, 1));

                        // Only include if in candidate set
                        std::unordered_set<int64_t> cand_set(candidate_file_ids.begin(), candidate_file_ids.end());
                        if (cand_set.count(h.file_id)) {
                            hits.push_back(h);
                        }
                    }
                    sqlite3_finalize(mem_stmt);
                }
            }

            // Look up file details
            const char* file_sql = "SELECT abs_path, display_name, size_bytes, mtime_unix FROM files WHERE id = ?";
            sqlite3_stmt* file_stmt = nullptr;
            if (sqlite3_prepare_v2(db, file_sql, -1, &file_stmt, nullptr) == SQLITE_OK) {
                for (const auto& h : hits) {
                    sqlite3_reset(file_stmt);
                    sqlite3_bind_int64(file_stmt, 1, h.file_id);

                    if (sqlite3_step(file_stmt) == SQLITE_ROW) {
                        search_result res;
                        res.file_id = h.file_id;
                        res.distance = 1.0f - h.score;

                        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 0));
                        res.abs_path = path ? path : "";

                        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 1));
                        res.display_name = name ? name : "";

                        res.size_bytes = sqlite3_column_int64(file_stmt, 2);
                        res.mtime_unix = sqlite3_column_int64(file_stmt, 3);

                        // Time-based Burst Shot Filter (NMS)
                        bool is_burst = false;
                        if (res.mtime_unix > 0) {
                            for (const auto& accepted : results) {
                                if (accepted.mtime_unix > 0 && std::abs(accepted.mtime_unix - res.mtime_unix) < 30) {
                                    is_burst = true;
                                    break;
                                }
                            }
                        }

                        if (!is_burst) {
                            results.push_back(res);
                        }
                    }
                }
                sqlite3_finalize(file_stmt);
            }
        }

    } else {
        // ── Step 6b: No candidates → global KNN (existing path) ──
        LOGI("No NER candidates, falling back to global KNN");

        // Use the corrected query (or clip_caption if available) for CLIP
        std::string final_query = clip_caption.empty() ? corrected_query : clip_caption;
        if (final_query.empty()) final_query = corrected_query;

        std::vector<int64_t> tokens = tokenizer->encode(final_query);
        std::vector<float> query_embedding;
        try {
            query_embedding = clip_session->get_text_embedding(tokens);
        } catch (const std::exception& e) {
            LOGE("Failed to get text embedding: %s", e.what());
            sqlite3_close(db);
            return results;
        }

        // KNN query
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
            }
            sqlite3_finalize(knn_stmt);
        }

        LOGI("KNN returned %zu hits", hits.size());

        // Look up file details
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
    }

    LOGI("Search complete: %zu results", results.size());
    sqlite3_close(db);
    return results;
}
