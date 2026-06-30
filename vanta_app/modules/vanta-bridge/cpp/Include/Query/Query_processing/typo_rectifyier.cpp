#include "typo_rectifyier.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cctype>

VantaProductionAnalyzer::VantaProductionAnalyzer(const SymSpellDictionary& symspell_dict, const std::vector<std::string>& whitelist_terms) 
    : dictionary(symspell_dict), max_edit_distance(2), confidence_threshold(0.75f) {
    
    // Initialize whitelist and auto-populate the dictionary with whitelist terms
    for (const auto& term : whitelist_terms) {
        std::string lower_term = term;
        std::transform(lower_term.begin(), lower_term.end(), lower_term.begin(), ::tolower);
        whitelist.insert(lower_term);
        
        // Auto-populate the mock dictionary so the rectifier works on these terms
        dictionary.exact_matches.insert(lower_term);
        auto deletes = get_deletes(lower_term, max_edit_distance);
        for (const auto& del : deletes) {
            // Fake a high frequency to ensure it gets picked
            dictionary.deletes_map[del].push_back({lower_term, 1000}); 
        }
    }
}

// 1. O(N) Manual Normalization
std::string VantaProductionAnalyzer::normalize_query(const std::string& query) {
    std::string normalized;
    normalized.reserve(query.length());
    
    char last_char = '\0';
    int consecutive_count = 0;

    for (char c : query) {
        char lower_c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        
        // Keep only alphanumeric and spaces (Note: extend this if supporting filename modes)
        if ((lower_c >= 'a' && lower_c <= 'z') || 
            (lower_c >= '0' && lower_c <= '9') || 
            lower_c == ' ') {
            
            if (lower_c == last_char && lower_c != ' ') {
                consecutive_count++;
            } else {
                consecutive_count = 1;
                last_char = lower_c;
            }

            // Preserve maximum of 2 consecutive characters (the \1\1 fix)
            if (consecutive_count <= 2) {
                normalized += lower_c;
            }
        }
    }
    
    // Trim leading/trailing spaces
    size_t first = normalized.find_first_not_of(' ');
    if (std::string::npos == first) return "";
    size_t last = normalized.find_last_not_of(' ');
    return normalized.substr(first, (last - first + 1));
}

// 2. Damerau-Levenshtein (1D vector mapped as a 2D table)
int VantaProductionAnalyzer::damerau_levenshtein(const std::string& s1, const std::string& s2) {
    int len1 = s1.length();
    int len2 = s2.length();
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    std::vector<int> d((len1 + 1) * (len2 + 1));
    auto get_idx = [len2](int i, int j) { return i * (len2 + 1) + j; };

    for (int i = 0; i <= len1; i++) d[get_idx(i, 0)] = i;
    for (int j = 0; j <= len2; j++) d[get_idx(0, j)] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;

            d[get_idx(i, j)] = std::min({
                d[get_idx(i - 1, j)] + 1,        // Deletion
                d[get_idx(i, j - 1)] + 1,        // Insertion
                d[get_idx(i - 1, j - 1)] + cost  // Substitution
            });

            // Transposition check (cost of 1 for swaps like 'vrey' -> 'very')
            if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
                d[get_idx(i, j)] = std::min(
                    d[get_idx(i, j)], 
                    d[get_idx(i - 2, j - 2)] + cost
                );
            }
        }
    }
    return d[get_idx(len1, len2)];
}

// 3. Stable, deterministic confidence scoring
float VantaProductionAnalyzer::calculate_confidence(int edit_dist, int freq) {
    float base = 1.0f;
    if (edit_dist == 1) base = 0.95f;
    else if (edit_dist == 2) base = 0.80f;
    else if (edit_dist > 2) return 0.0f;

    // Small frequency boost (max +0.05) to break ties gracefully
    float freq_boost = std::min(0.05f, static_cast<float>(std::log10(std::max(freq, 1)) * 0.01f));
    
    return std::min(1.0f, base + freq_boost);
}

// 4. Generate query deletes (Phase 2 optimization target)
std::unordered_set<std::string> VantaProductionAnalyzer::get_deletes(const std::string& word, int max_dist) {
    std::unordered_set<std::string> deletes{word};
    std::vector<std::string> queue{word};

    for (int d = 0; d < max_dist; d++) {
        std::vector<std::string> next_queue;
        for (const auto& w : queue) {
            for (size_t i = 0; i < w.length(); i++) {
                std::string deleted = w.substr(0, i) + w.substr(i + 1);
                if (deletes.insert(deleted).second) {
                    next_queue.push_back(deleted);
                }
            }
        }
        queue = std::move(next_queue);
    }
    return deletes;
}

// 5. Token Correction Logic
CorrectionResult VantaProductionAnalyzer::correct_token(const std::string& token) {
    // Zero-cost Digit Protection (Protects qwen3, gpt4, mp4)
    if (std::any_of(token.begin(), token.end(), [](unsigned char c){ return std::isdigit(c); })) {
        return {token, token, 1.0f, 0};
    }

    // Whitelist & Exact Match Check
    if (whitelist.count(token) || dictionary.exact_matches.count(token)) {
        return {token, token, 1.0f, 0};
    }

    std::string best_word = token;
    float best_confidence = 0.0f;
    int best_distance = 999;
    int best_freq = 0;

    std::unordered_set<std::string> seen_candidates;

    // Tiered distance generation: Checks dist 1 first, only checks dist 2 if needed
    for (int current_dist = 1; current_dist <= max_edit_distance; current_dist++) {
        auto deletes = get_deletes(token, current_dist);

        for (const auto& deleted_str : deletes) {
            auto it = dictionary.deletes_map.find(deleted_str);
            if (it == dictionary.deletes_map.end()) continue;

            for (const auto& candidate_pair : it->second) {
                const std::string& candidate = candidate_pair.first;
                int freq = candidate_pair.second;

                // Skip if we've already evaluated this candidate
                if (!seen_candidates.insert(candidate).second) continue;

                int actual_distance = damerau_levenshtein(token, candidate);
                if (actual_distance > max_edit_distance) continue;

                float confidence = calculate_confidence(actual_distance, freq);
                bool better = false;

                // Prioritize highest confidence, tie-break with frequency & shortest actual distance
                if (confidence > best_confidence) {
                    better = true;
                } else if (std::abs(confidence - best_confidence) < 1e-9) {
                    if (actual_distance < best_distance) {
                        better = true;
                    } else if (actual_distance == best_distance && freq > best_freq) {
                        better = true;
                    }
                }

                if (better) {
                    best_word = candidate;
                    best_confidence = confidence;
                    best_distance = actual_distance;
                    best_freq = freq;
                }
            }
        }

        // Early exit: if we find a highly confident match at dist 1, skip dist 2
        if (best_confidence >= confidence_threshold) {
            break; 
        }
    }

    if (best_confidence >= confidence_threshold) {
        return {best_word, token, best_confidence, best_distance};
    }

    // Conservative fallback
    return {token, token, 0.0f, 0};
}

// 6. Main Pipeline Entry Point
QueryAnalysis VantaProductionAnalyzer::analyze(const std::string& raw_query) {
    std::string normalized = normalize_query(raw_query);
    
    QueryAnalysis result;
    result.original_query = normalized;
    result.was_corrected = false;
    
    std::istringstream iss(normalized);
    std::string token;
    
    bool first = true;
    while (iss >> token) {
        CorrectionResult res;
        if (token.length() <= 2) {
            // Ignore extremely short tokens
            res = {token, token, 1.0f, 0};
        } else {
            res = correct_token(token);
        }
        
        result.token_corrections.push_back(res);

        if (res.word != res.original) {
            result.was_corrected = true;
        }

        if (!first) result.corrected_query += " ";
        result.corrected_query += res.word;
        first = false;
    }

    return result;
}
