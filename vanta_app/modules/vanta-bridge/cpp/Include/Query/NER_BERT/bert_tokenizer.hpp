#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace vanta {
namespace ner {

/**
 * Minimal BERT WordPiece tokenizer for the NER model.
 *
 * Loads a tab-separated vocab.txt (token\tid) produced from the HuggingFace
 * tokenizer.json and encodes a raw query into the [CLS] ... [SEP] form the
 * ONNX model expects.
 */
class BertTokenizer {
public:
    BertTokenizer();

    /** Load vocabulary from a tab-separated vocab.txt file. */
    bool load(const std::string& vocab_path);

    /** True after a successful load(). */
    bool is_loaded() const { return loaded_; }

    /**
     * Encode a raw user query.
     *
     * @param text Input query.
     * @param max_length Model max sequence length (32 for the NER tiny model).
     * @param input_ids Output token IDs, padded to max_length.
     * @param attention_mask Output attention mask.
     * @param token_type_ids Output token type IDs (all zeros for single sentence).
     * @param word_ids Output mapping from each token position to the source word
     *                 index, or -1 for special/padding tokens.
     * @param words Output list of pre-tokenized words (for label decoding).
     */
    bool encode(
        const std::string& text,
        int max_length,
        std::vector<int64_t>& input_ids,
        std::vector<int64_t>& attention_mask,
        std::vector<int64_t>& token_type_ids,
        std::vector<int>& word_ids,
        std::vector<std::string>& words
    ) const;

private:
    bool loaded_ = false;
    std::unordered_map<std::string, int> vocab_;

    static std::string normalize(const std::string& s);
    static std::vector<std::string> basic_tokenize(const std::string& s);
    std::vector<std::string> wordpiece_tokenize(const std::string& word) const;
};

} // namespace ner
} // namespace vanta
