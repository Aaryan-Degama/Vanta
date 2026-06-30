#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

struct CorrectionResult {
    std::string word;
    std::string original;
    float confidence;
    int edit_distance;
};

struct QueryAnalysis {
    std::string original_query;
    std::string corrected_query;
    std::vector<CorrectionResult> token_corrections;
    bool was_corrected; 
};

// Mock definition of the dictionary holding precomputed SymSpell deletes
// In production, this would likely be mapped from a FlatBuffer/binary file.
struct SymSpellDictionary {
    std::unordered_set<std::string> exact_matches;
    // Maps a deleted string to a list of pairs: {original_word, frequency}
    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> deletes_map;
};

// --- Core Analyzer ---
class VantaProductionAnalyzer {
private:
    SymSpellDictionary dictionary;
    std::unordered_set<std::string> whitelist;
    int max_edit_distance;
    float confidence_threshold;

public:
    VantaProductionAnalyzer(const SymSpellDictionary& symspell_dict, const std::vector<std::string>& whitelist_terms);

    // 1. O(N) Manual Normalization
    std::string normalize_query(const std::string& query);

    // 2. Damerau-Levenshtein (1D vector mapped as a 2D table)
    int damerau_levenshtein(const std::string& s1, const std::string& s2);

    // 3. Stable, deterministic confidence scoring
    float calculate_confidence(int edit_dist, int freq);

    // 4. Generate query deletes (Phase 2 optimization target)
    std::unordered_set<std::string> get_deletes(const std::string& word, int max_dist);

    // 5. Token Correction Logic
    CorrectionResult correct_token(const std::string& token);

    // 6. Main Pipeline Entry Point
    QueryAnalysis analyze(const std::string& raw_query);
};
