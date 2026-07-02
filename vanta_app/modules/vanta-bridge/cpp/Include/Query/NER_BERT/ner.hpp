#pragma once

/**
 * @file ner.hpp
 * Named Entity Recognition model for the Vanta search pipeline.
 *
 * Wraps a BERT-based ONNX model that tags query tokens with BIO labels
 * (SELF, PERSON, RELATION). The extracted spans are used by the query
 * engine to resolve people and route searches through the entity graph.
 */

#include "bert_tokenizer.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <onnxruntime_cxx_api.h>

namespace vanta {
namespace ner {

/**
 * A contiguous span of words sharing the same entity label.
 */
struct NERSpan {
    std::string label;   // "SELF", "PERSON", "RELATION" (prefix stripped)
    std::string text;    // original words, space-joined
};

/**
 * BERT-based NER model that produces BIO-tagged spans from a query string.
 *
 * Thread safety: the ONNX session is NOT thread-safe; callers must serialise
 * access to run(). In practice the global g_ner_model in vanta.cpp is
 * protected by g_session_mutex.
 */
class NERModel {
public:
    NERModel() = default;
    ~NERModel();

    /**
     * Loads the ONNX model, vocab, and label map from disk.
     *
     * @param model_path      Path to ner_model.onnx
     * @param vocab_path      Path to vocab.txt (tab-separated)
     * @param label_map_path  Path to label_map.json
     * @return true on success.
     */
    bool load(const std::string& model_path,
              const std::string& vocab_path,
              const std::string& label_map_path);

    bool is_loaded() const { return loaded_; }

    /**
     * Runs NER inference on a raw query string.
     *
     * @param query  The (possibly typo-corrected) user query.
     * @return Vector of extracted spans. O-tagged words are excluded.
     */
    std::vector<NERSpan> run(const std::string& query) const;

private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "NERModel"};
    Ort::Session* session_ = nullptr;
    BertTokenizer tokenizer_;
    std::unordered_map<int, std::string> id2label_;
    bool loaded_ = false;
    int max_len_ = 32;

    /**
     * Parses label_map.json manually (the file is tiny).
     */
    bool parse_label_map(const std::string& path);

    /**
     * Converts per-word BIO labels into contiguous NERSpan objects.
     *
     * Consecutive B-X starts a new span. I-X continues the current span
     * only if its label matches; otherwise starts a new span. O closes
     * any open span.
     */
    std::vector<NERSpan> decode_spans(
        const std::vector<std::string>& words,
        const std::vector<std::string>& word_labels) const;
};

} // namespace ner
} // namespace vanta
