#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace vanta {
namespace query {

class IntentBuilder {
public:
    IntentBuilder();

    /**
     * @brief Transforms a raw/typo-corrected user query into a CLIP-friendly visual description.
     * 
     * @param originalQuery The raw query input by the user.
     * @param correctedQuery The typo-corrected query.
     * @return std::string A concise, visual description optimized for CLIP embeddings.
     */
    std::string buildIntent(const std::string& originalQuery, const std::string& correctedQuery) const;

private:
    // Core mappings for translating text to visual concepts
    std::unordered_map<std::string, std::string> peopleMap;
    std::unordered_map<std::string, std::string> placesMap;
    std::unordered_map<std::string, std::string> eventsMap;
    std::unordered_set<std::string> fillerWords;

    // Helper structure
    struct TokenInfo;
    
    // Internal methods
    std::pair<std::vector<TokenInfo>, bool> tokenizeInternal(const std::string& query) const;
    

};

} // namespace query
} // namespace vanta
