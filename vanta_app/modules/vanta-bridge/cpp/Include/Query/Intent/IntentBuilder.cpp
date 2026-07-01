#include "IntentBuilder.hpp"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "IntentBuilder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#endif

namespace vanta {
namespace query {

struct IntentBuilder::TokenInfo {
    std::string lower;
    bool isCapitalized;
};

IntentBuilder::IntentBuilder() {
    fillerWords = {
        "show", "me", "pictures", "picture", "photos", "photo", "of", "and", "in", "at", 
        "on", "the", "a", "an", "please", "find", "my", "some", "i", "want", "to", "see",
        "with", "images", "image", "give", "looking", "for"
    };

    peopleMap = {
        {"me", "portrait of a person"},
        {"myself", "portrait of a person"},
        {"self", "portrait of a person"},
        {"family", "family photo"},
        {"group", "group of people"},
        {"people", "group of people"},
        {"father", "portrait of a man"},
        {"dad", "portrait of a man"},
        {"mother", "portrait of a woman"},
        {"mom", "portrait of a woman"},
        {"brother", "portrait of a man"},
        {"sister", "portrait of a woman"},
        {"friend", "portrait of a person"},
        {"friends", "group of people"},
        {"wife", "portrait of a woman"},
        {"husband", "portrait of a man"},
        {"uncle", "portrait of a man"},
        {"aunt", "portrait of a woman"},
        {"cousin", "portrait of a person"}
    };

    placesMap = {
        {"beach", "at a beach"},
        {"mountain", "in the mountains"},
        {"mountains", "in the mountains"},
        {"kedarnath", "at kedarnath"},
        {"temple", "near a temple"},
        {"office", "in an office"},
        {"school", "at school"},
        {"home", "at home"},
        {"park", "in a park"},
        {"restaurant", "at a restaurant"}
    };

    eventsMap = {
        {"graduation", "graduation ceremony"},
        {"birthday", "birthday celebration"},
        {"wedding", "wedding ceremony"},
        {"diwali", "diwali celebration"},
        {"holi", "holi celebration"},
        {"festival", "festival celebration"},
        {"vacation", "holiday trip"},
        {"trip", "holiday trip"}
    };
}

std::pair<std::vector<IntentBuilder::TokenInfo>, bool> IntentBuilder::tokenizeInternal(const std::string& query) const {
    std::string text = query;
    std::string lowerQuery = text;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    std::vector<std::string> prefixes = {
        "show me pictures of ", "show me photos of ", "show me images of ",
        "show me a picture of ", "show me a photo of ", "show me an image of ",
        "find pictures of ", "find photos of ", "find images of ",
        "please find pictures of ", "please find photos of ",
        "pictures of ", "photos of ", "images of ", "picture of ", "photo of ", "image of ",
        "show me ", "find me ", "please show me ", "please find ", "looking for "
    };
    
    bool strippedMe = false;
    for (const auto& prefix : prefixes) {
        size_t pos = lowerQuery.find(prefix);
        if (pos == 0) {
            text.erase(pos, prefix.length());
            // If the prefix ends with "me ", it means "me" was the direct object
            // of the verb before the rest of the sentence. e.g. "find me ", "show me "
            if (prefix.length() >= 3 && prefix.substr(prefix.length() - 3) == "me ") {
                strippedMe = true;
            }
            break;
        }
    }
    
    std::vector<TokenInfo> tokens;
    std::string currentRaw = "";
    
    for (char c : text) {
        if (std::isalpha(c)) {
            currentRaw += c;
        } else if (c == ' ' || c == '\t' || c == '\n') {
            if (!currentRaw.empty()) {
                TokenInfo info;
                info.isCapitalized = std::isupper(currentRaw[0]);
                info.lower = currentRaw;
                std::transform(info.lower.begin(), info.lower.end(), info.lower.begin(),
                    [](unsigned char ch){ return std::tolower(ch); });
                tokens.push_back(info);
                currentRaw.clear();
            }
        }
    }
    if (!currentRaw.empty()) {
        TokenInfo info;
        info.isCapitalized = std::isupper(currentRaw[0]);
        info.lower = currentRaw;
        std::transform(info.lower.begin(), info.lower.end(), info.lower.begin(),
            [](unsigned char ch){ return std::tolower(ch); });
        tokens.push_back(info);
    }
    
    return {tokens, strippedMe};
}

std::string IntentBuilder::buildIntent(const std::string& originalQuery, const std::string& correctedQuery) const {
    std::string queryToUse = correctedQuery.empty() ? originalQuery : correctedQuery;
    
    auto tokenResult = tokenizeInternal(queryToUse);
    const std::vector<TokenInfo>& tokens = tokenResult.first;
    bool strippedMe = tokenResult.second;
    
    // 1. Extract Event (Highest Priority)
    std::string eventIntent = "";
    for (const auto& token : tokens) {
        auto it = eventsMap.find(token.lower);
        if (it != eventsMap.end()) {
            eventIntent = it->second;
            break;
        }
    }
    
    // 2. Extract Place
    std::string placeIntent = "";
    for (const auto& token : tokens) {
        auto it = placesMap.find(token.lower);
        if (it != placesMap.end()) {
            placeIntent = it->second;
            break;
        }
    }
    
    // 3. Extract People
    int personCount = 0;
    bool hasFamily = false;
    bool hasGroup = false;
    std::string lastPersonMatch = "";
    
    for (const auto& token : tokens) {
        if (token.lower == "family") {
            hasFamily = true;
        } else if (token.lower == "group" || token.lower == "friends" || token.lower == "people") {
            hasGroup = true;
        } else {
            auto it = peopleMap.find(token.lower);
            if (it != peopleMap.end()) {
                personCount++;
                if (token.lower != "me" && token.lower != "myself" && token.lower != "self") {
                    lastPersonMatch = it->second;
                } else if (lastPersonMatch.empty()) {
                    lastPersonMatch = it->second;
                }
            } else if (token.isCapitalized && fillerWords.find(token.lower) == fillerWords.end() && 
                       placesMap.find(token.lower) == placesMap.end() && eventsMap.find(token.lower) == eventsMap.end()) {
                // Heuristic: Capitalized unknown word is likely a name (person)
                personCount++;
            }
        }
    }
    
    std::string peopleIntent = "";
    if (hasFamily) {
        peopleIntent = "family photo";
    } else if (hasGroup || personCount > 2) {
        peopleIntent = "group of people";
    } else if (personCount == 2) {
        peopleIntent = "two people";
    } else if (personCount == 1) {
        if (lastPersonMatch.empty()) {
            peopleIntent = "portrait of a person";
        } else {
            peopleIntent = lastPersonMatch;
        }
    } else if (personCount == 0 && strippedMe) {
        // If we stripped 'me' and found no other people/events, 'me' is the subject
        peopleIntent = "portrait of a person";
    }
    
    // Assemble final intent
    std::string coreSubject = eventIntent;
    
    if (coreSubject.empty()) {
        if (!peopleIntent.empty()) {
            coreSubject = peopleIntent;
        }
    }
    
    std::string result = coreSubject;
    if (!placeIntent.empty()) {
        if (!result.empty()) {
            result += " " + placeIntent;
        } else {
            result = placeIntent;
        }
    }
    
    LOGI("Generated Intent: %s", result.c_str());
    
    return result;
}

} // namespace query
} // namespace vanta
