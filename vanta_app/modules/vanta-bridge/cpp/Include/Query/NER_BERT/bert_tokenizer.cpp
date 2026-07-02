#include "bert_tokenizer.hpp"

#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#define LOG_TAG "VantaBertTokenizer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace vanta {
namespace ner {

namespace {

constexpr int CLS_ID = 101;
constexpr int SEP_ID = 102;
constexpr int PAD_ID = 0;
constexpr int UNK_ID = 100;

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_punctuation(char c) {
    // Sufficient for query tokenization: standard ASCII punctuation.
    return c == '!' || c == '"' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' ||
           c == '+' || c == ',' || c == '-' || c == '.' || c == '/' ||
           c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' ||
           c == '^' || c == '_' || c == '`' || c == '{' || c == '|' ||
           c == '}' || c == '~';
}

} // namespace

BertTokenizer::BertTokenizer() = default;

bool BertTokenizer::load(const std::string& vocab_path) {
    std::ifstream ifs(vocab_path);
    if (!ifs.is_open()) {
        LOGE("Failed to open vocab file: %s", vocab_path.c_str());
        return false;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;

        std::string token = line.substr(0, tab);
        int id = std::stoi(line.substr(tab + 1));
        vocab_[token] = id;
    }

    if (vocab_.empty()) {
        LOGE("Vocab file is empty: %s", vocab_path.c_str());
        return false;
    }

    loaded_ = true;
    return true;
}

std::string BertTokenizer::normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalpha(c)) {
            out += static_cast<char>(std::tolower(c));
        } else if (std::isdigit(c) || is_whitespace(c) || is_punctuation(c)) {
            out += static_cast<char>(c);
        } else {
            // Drop other characters for simplicity.
            out += ' ';
        }
    }
    return out;
}

std::vector<std::string> BertTokenizer::basic_tokenize(const std::string& s) {
    std::vector<std::string> words;
    std::string current;

    for (char c : s) {
        if (is_whitespace(c)) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else if (is_punctuation(c)) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            words.emplace_back(1, c);
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        words.push_back(current);
    }

    return words;
}

std::vector<std::string> BertTokenizer::wordpiece_tokenize(const std::string& word) const {
    std::vector<std::string> tokens;
    std::string remaining = word;

    while (!remaining.empty()) {
        size_t end = remaining.size();
        std::string subword;
        bool found = false;

        while (end > 0) {
            subword = remaining.substr(0, end);
            if (vocab_.find(subword) != vocab_.end()) {
                found = true;
                break;
            }
            --end;
        }

        if (!found) {
            tokens.push_back("[UNK]");
            break;
        }

        tokens.push_back(subword);
        if (end == remaining.size()) break;

        remaining = "##" + remaining.substr(end);
    }

    return tokens;
}

bool BertTokenizer::encode(
    const std::string& text,
    int max_length,
    std::vector<int64_t>& input_ids,
    std::vector<int64_t>& attention_mask,
    std::vector<int64_t>& token_type_ids,
    std::vector<int>& word_ids,
    std::vector<std::string>& words
) const {
    if (!loaded_) return false;

    input_ids.assign(max_length, PAD_ID);
    attention_mask.assign(max_length, 0);
    token_type_ids.assign(max_length, 0);
    word_ids.assign(max_length, -1);

    words = basic_tokenize(normalize(text));

    std::vector<int> token_ids;
    std::vector<int> token_word_ids;

    token_ids.push_back(CLS_ID);
    token_word_ids.push_back(-1);

    for (size_t w = 0; w < words.size(); ++w) {
        auto sub_tokens = wordpiece_tokenize(words[w]);
        for (const auto& sub : sub_tokens) {
            auto it = vocab_.find(sub);
            token_ids.push_back(it != vocab_.end() ? it->second : UNK_ID);
            token_word_ids.push_back(static_cast<int>(w));
        }
    }

    token_ids.push_back(SEP_ID);
    token_word_ids.push_back(-1);

    // Truncate if needed, keeping [CLS] and [SEP].
    if (static_cast<int>(token_ids.size()) > max_length) {
        token_ids.resize(max_length - 1);
        token_word_ids.resize(max_length - 1);
        token_ids.push_back(SEP_ID);
        token_word_ids.push_back(-1);
    }

    for (size_t i = 0; i < token_ids.size(); ++i) {
        input_ids[i] = token_ids[i];
        attention_mask[i] = 1;
        word_ids[i] = token_word_ids[i];
    }

    return true;
}

} // namespace ner
} // namespace vanta
