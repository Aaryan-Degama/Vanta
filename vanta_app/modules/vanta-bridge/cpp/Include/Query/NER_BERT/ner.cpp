#include "ner.hpp"

#include <android/log.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>

#define LOG_TAG "VantaNER"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace vanta {
namespace ner {

NERModel::~NERModel() {
    delete session_;
    session_ = nullptr;
}

// ---------------------------------------------------------------------------
// Minimal label_map.json parser
// ---------------------------------------------------------------------------
// The file looks like:
//   { "id2label": { "0": "O", "1": "B-SELF", ... }, ... }
// We only care about the id2label mapping.
bool NERModel::parse_label_map(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        LOGE("Failed to open label_map.json: %s", path.c_str());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    // Find "id2label" block
    size_t pos = content.find("\"id2label\"");
    if (pos == std::string::npos) {
        LOGE("label_map.json missing id2label key");
        return false;
    }

    // Find the opening brace of the id2label object
    pos = content.find('{', pos);
    if (pos == std::string::npos) return false;
    pos++; // skip '{'

    // Parse key-value pairs: "0": "O", "1": "B-SELF", ...
    while (pos < content.size()) {
        // Skip whitespace/commas
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\n' ||
               content[pos] == '\r' || content[pos] == '\t' || content[pos] == ',')) {
            pos++;
        }
        if (pos >= content.size() || content[pos] == '}') break;

        // Parse key (quoted integer)
        if (content[pos] != '"') break;
        pos++;
        size_t key_end = content.find('"', pos);
        if (key_end == std::string::npos) break;
        int id = std::stoi(content.substr(pos, key_end - pos));
        pos = key_end + 1;

        // Skip colon and whitespace
        while (pos < content.size() && (content[pos] == ':' || content[pos] == ' ')) pos++;

        // Parse value (quoted string)
        if (pos >= content.size() || content[pos] != '"') break;
        pos++;
        size_t val_end = content.find('"', pos);
        if (val_end == std::string::npos) break;
        std::string label = content.substr(pos, val_end - pos);
        pos = val_end + 1;

        id2label_[id] = label;
    }

    if (id2label_.empty()) {
        LOGE("Parsed zero labels from label_map.json");
        return false;
    }

    LOGI("Loaded %zu NER labels", id2label_.size());
    return true;
}

// ---------------------------------------------------------------------------
// Load model, tokenizer, and label map
// ---------------------------------------------------------------------------
bool NERModel::load(const std::string& model_path,
                    const std::string& vocab_path,
                    const std::string& label_map_path) {
    if (loaded_) return true;

    // 1. Load label map
    if (!parse_label_map(label_map_path)) {
        return false;
    }

    // 2. Load tokenizer vocabulary
    if (!tokenizer_.load(vocab_path)) {
        LOGE("Failed to load NER vocab from %s", vocab_path.c_str());
        return false;
    }

    // 3. Create ONNX session — single-threaded, no NNAPI (BERT ops not
    //    reliably supported on all NNAPI implementations).
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = new Ort::Session(env_, model_path.c_str(), opts);
        LOGI("NER ONNX session created: %s", model_path.c_str());
    } catch (const Ort::Exception& e) {
        LOGE("Failed to create NER ONNX session: %s", e.what());
        return false;
    }

    loaded_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// BIO span decoding
// ---------------------------------------------------------------------------
std::vector<NERSpan> NERModel::decode_spans(
    const std::vector<std::string>& words,
    const std::vector<std::string>& word_labels) const {

    std::vector<NERSpan> spans;

    auto strip_prefix = [](const std::string& label) -> std::string {
        // "B-SELF" → "SELF", "I-PERSON" → "PERSON"
        if (label.size() > 2 && label[1] == '-') {
            return label.substr(2);
        }
        return label;
    };

    NERSpan current;
    bool in_span = false;

    for (size_t i = 0; i < words.size() && i < word_labels.size(); ++i) {
        const std::string& label = word_labels[i];

        if (label == "O") {
            // Close any open span
            if (in_span) {
                spans.push_back(current);
                in_span = false;
            }
        } else if (label.size() > 2 && label[0] == 'B') {
            // B-X: start a new span (close previous if open)
            if (in_span) {
                spans.push_back(current);
            }
            current.label = strip_prefix(label);
            current.text = words[i];
            in_span = true;
        } else if (label.size() > 2 && label[0] == 'I') {
            // I-X: continue current span only if label matches
            std::string base = strip_prefix(label);
            if (in_span && current.label == base) {
                current.text += " " + words[i];
            } else {
                // Label mismatch — treat as new span
                if (in_span) {
                    spans.push_back(current);
                }
                current.label = base;
                current.text = words[i];
                in_span = true;
            }
        }
    }

    // Close any trailing span
    if (in_span) {
        spans.push_back(current);
    }

    return spans;
}

// ---------------------------------------------------------------------------
// Run NER inference
// ---------------------------------------------------------------------------
std::vector<NERSpan> NERModel::run(const std::string& query) const {
    std::vector<NERSpan> empty;
    if (!loaded_ || !session_) return empty;

    // 1. Tokenize
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    std::vector<int64_t> token_type_ids;
    std::vector<int> word_ids;
    std::vector<std::string> words;

    if (!tokenizer_.encode(query, max_len_,
                           input_ids, attention_mask, token_type_ids,
                           word_ids, words)) {
        LOGE("NER tokenization failed for query: %s", query.c_str());
        return empty;
    }

    // 2. Build input tensors — shape [1, max_len]
    std::array<int64_t, 2> shape = {1, static_cast<int64_t>(max_len_)};
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_ids_tensor = Ort::Value::CreateTensor<int64_t>(
        mem_info, input_ids.data(), input_ids.size(),
        shape.data(), shape.size());

    Ort::Value attention_mask_tensor = Ort::Value::CreateTensor<int64_t>(
        mem_info, attention_mask.data(), attention_mask.size(),
        shape.data(), shape.size());

    Ort::Value token_type_ids_tensor = Ort::Value::CreateTensor<int64_t>(
        mem_info, token_type_ids.data(), token_type_ids.size(),
        shape.data(), shape.size());

    // 3. Run inference
    const char* input_names[] = {"input_ids", "attention_mask", "token_type_ids"};
    const char* output_names[] = {"logits"};

    std::vector<Ort::Value> input_tensors;
    input_tensors.push_back(std::move(input_ids_tensor));
    input_tensors.push_back(std::move(attention_mask_tensor));
    input_tensors.push_back(std::move(token_type_ids_tensor));

    std::vector<Ort::Value> output_tensors;
    try {
        output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, input_tensors.data(), input_tensors.size(),
            output_names, 1);
    } catch (const Ort::Exception& e) {
        LOGE("NER inference failed: %s", e.what());
        return empty;
    }

    // 4. Process output: logits shape [1, max_len, num_labels]
    auto& logits_tensor = output_tensors[0];
    auto logits_shape = logits_tensor.GetTensorTypeAndShapeInfo().GetShape();
    // logits_shape = [1, max_len, num_labels]
    int seq_len = static_cast<int>(logits_shape[1]);
    int num_labels = static_cast<int>(logits_shape[2]);
    const float* logits = logits_tensor.GetTensorData<float>();

    // 5. Argmax per position → per-word labels
    //    - Skip positions where word_ids == -1 (special tokens)
    //    - Skip subword continuations (word_ids[i] == word_ids[i-1])
    std::vector<std::string> per_word_labels(words.size(), "O");

    for (int i = 0; i < seq_len; ++i) {
        int wid = word_ids[i];
        if (wid < 0) continue; // CLS, SEP, PAD

        // Only use the FIRST subword token for each word
        if (i > 0 && word_ids[i - 1] == wid) continue;

        // Argmax over label dimension
        const float* row = logits + i * num_labels;
        int best_label = 0;
        float best_score = row[0];
        for (int j = 1; j < num_labels; ++j) {
            if (row[j] > best_score) {
                best_score = row[j];
                best_label = j;
            }
        }

        auto it = id2label_.find(best_label);
        if (it != id2label_.end() && static_cast<size_t>(wid) < words.size()) {
            per_word_labels[wid] = it->second;
        }
    }

    LOGI("NER results for '%s': %zu words", query.c_str(), words.size());
    for (size_t i = 0; i < words.size(); ++i) {
        LOGI("  word[%zu]='%s' → %s", i, words[i].c_str(), per_word_labels[i].c_str());
    }

    // 6. Decode BIO labels into spans
    return decode_spans(words, per_word_labels);
}

} // namespace ner
} // namespace vanta
