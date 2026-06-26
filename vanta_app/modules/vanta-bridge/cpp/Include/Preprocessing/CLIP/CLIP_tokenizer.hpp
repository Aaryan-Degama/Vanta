#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// Hashing for std::pair
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

class CLIPTokenizer {
public:
    CLIPTokenizer();
    bool load(const std::string& vocab_path, const std::string& merges_path);
    std::vector<int64_t> encode(const std::string& text, int max_length = 77);
    bool is_loaded() const { return loaded_; }

private:
    bool loaded_ = false;
    std::unordered_map<std::string, int> vocab_;
    std::unordered_map<std::pair<std::string, std::string>, int, pair_hash> bpe_ranks_;

    std::vector<std::string> bpe(const std::string& word);
    std::vector<std::string> get_pairs(const std::vector<std::string>& word);
};
