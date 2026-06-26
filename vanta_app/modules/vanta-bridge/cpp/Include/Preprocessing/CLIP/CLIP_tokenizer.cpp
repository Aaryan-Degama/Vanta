#include "CLIP_tokenizer.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <android/log.h>

#define LOG_TAG "VantaTokenizer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

CLIPTokenizer::CLIPTokenizer() {}

bool CLIPTokenizer::load(const std::string& vocab_path, const std::string& merges_path) {
    if (loaded_) return true;

    // Load vocab.json manually
    std::ifstream vfile(vocab_path);
    if (!vfile.is_open()) {
        LOGE("Could not open vocab.json at %s", vocab_path.c_str());
        return false;
    }
    
    std::stringstream buffer;
    buffer << vfile.rdbuf();
    std::string vocab_str = buffer.str();
    
    size_t pos = 0;
    while ((pos = vocab_str.find('"', pos)) != std::string::npos) {
        size_t end_quote = pos + 1;
        bool found_end = false;
        while (end_quote < vocab_str.size()) {
            if (vocab_str[end_quote] == '"' && vocab_str[end_quote - 1] != '\\') {
                found_end = true;
                break;
            }
            end_quote++;
        }
        
        if (!found_end) break;
        
        std::string key = vocab_str.substr(pos + 1, end_quote - pos - 1);
        
        // Handle escaped quotes in key
        size_t esc_pos = 0;
        while ((esc_pos = key.find("\\\"", esc_pos)) != std::string::npos) {
            key.replace(esc_pos, 2, "\"");
            esc_pos += 1;
        }

        size_t colon = vocab_str.find(':', end_quote);
        if (colon == std::string::npos) break;
        
        size_t comma = vocab_str.find_first_of(",}", colon);
        if (comma == std::string::npos) break;
        
        std::string val_str = vocab_str.substr(colon + 1, comma - colon - 1);
        try {
            int val = std::stoi(val_str);
            vocab_[key] = val;
        } catch(...) {
            // Ignore parse errors
        }
        
        pos = comma;
    }
    
    // Load merges.txt
    std::ifstream mfile(merges_path);
    if (!mfile.is_open()) {
        LOGE("Could not open merges.txt at %s", merges_path.c_str());
        return false;
    }
    
    std::string line;
    int rank = 0;
    while (std::getline(mfile, line)) {
        if (line.empty() || line.find("#version") != std::string::npos) {
            continue;
        }
        size_t space = line.find(' ');
        if (space != std::string::npos) {
            std::string p1 = line.substr(0, space);
            std::string p2 = line.substr(space + 1);
            // Trim carriage returns if any
            if (!p2.empty() && p2.back() == '\r') p2.pop_back();
            bpe_ranks_[{p1, p2}] = rank++;
        }
    }
    
    loaded_ = true;
    return true;
}

std::vector<std::string> CLIPTokenizer::get_pairs(const std::vector<std::string>& word) {
    std::vector<std::string> pairs;
    if (word.size() < 2) return pairs;
    for (size_t i = 0; i < word.size() - 1; ++i) {
        pairs.push_back(word[i]);
        pairs.push_back(word[i+1]);
    }
    return pairs;
}

std::vector<std::string> CLIPTokenizer::bpe(const std::string& token) {
    std::vector<std::string> word;
    for (size_t i = 0; i < token.length(); ++i) {
        if (i == token.length() - 1) {
            word.push_back(std::string(1, token[i]) + "</w>");
        } else {
            word.push_back(std::string(1, token[i]));
        }
    }
    
    while (word.size() > 1) {
        int min_rank = 1e9;
        std::pair<std::string, std::string> min_pair;
        int min_idx = -1;
        
        for (size_t i = 0; i < word.size() - 1; ++i) {
            std::pair<std::string, std::string> p = {word[i], word[i+1]};
            if (bpe_ranks_.find(p) != bpe_ranks_.end()) {
                if (bpe_ranks_[p] < min_rank) {
                    min_rank = bpe_ranks_[p];
                    min_pair = p;
                    min_idx = i;
                }
            }
        }
        
        if (min_idx == -1) break; // No more merges possible
        
        std::vector<std::string> new_word;
        for (size_t i = 0; i < word.size(); ++i) {
            if (i == min_idx) {
                new_word.push_back(word[i] + word[i+1]);
                i++; // skip next
            } else {
                new_word.push_back(word[i]);
            }
        }
        word = new_word;
    }
    return word;
}

std::vector<int64_t> CLIPTokenizer::encode(const std::string& text, int max_length) {
    std::vector<int64_t> tokens;
    if (!loaded_) return tokens;
    
    int bos = 49406;
    int eos = 49407;
    
    tokens.push_back(bos);
    
    // Lowercase
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    // Simple word extraction: alphanumeric words or individual punctuation
    std::regex re("[a-z0-9]+|[^a-z0-9\\s]+");
    auto words_begin = std::sregex_iterator(lower_text.begin(), lower_text.end(), re);
    auto words_end = std::sregex_iterator();
    
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string word = match.str();
        
        std::vector<std::string> bpe_tokens = bpe(word);
        for (const std::string& bpt : bpe_tokens) {
            if (vocab_.find(bpt) != vocab_.end()) {
                tokens.push_back(vocab_[bpt]);
            }
        }
        
        if (tokens.size() >= max_length - 1) {
            break;
        }
    }
    
    if (tokens.size() >= max_length) {
        tokens.resize(max_length - 1);
    }
    tokens.push_back(eos);
    
    while (tokens.size() < max_length) {
        tokens.push_back(0);
    }
    
    return tokens;
}
