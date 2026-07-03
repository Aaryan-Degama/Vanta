# 09 — Model Assets & ONNX Runtime

> How Vanta manages, loads, and runs ML models on-device via ONNX Runtime.

---

## Model Inventory

| Model | File | Size | Purpose | Source |
|-------|------|------|---------|--------|
| **CLIP Image** | `clip_image_fp16.onnx` | ~170 MB | 224×224 → 512-dim image embedding | OpenAI CLIP ViT-B/32 |
| **CLIP Text** | `clip_text_fp16.onnx` | ~120 MB | Token IDs → 512-dim text embedding | OpenAI CLIP ViT-B/32 |
| **Tokenizer** | `bpe_simple_vocab_16e6.txt` | ~1 MB | BPE vocabulary for CLIP text tokenization | OpenAI CLIP |
| **Face Detection** | `det_10g.onnx` | 4.3 MB | Face detection (RetinaFace) | InsightFace ArcFace |
| **Face Recognition** | `w600k_r50.onnx` | 43.8 MB | 112×112 face → 512-dim embedding | InsightFace ArcFace |
| **NER** | `ner_model.onnx` | ~66 MB | Text → BIO labels for query parsing | DistilBERT fine-tuned |

**Total model footprint:** ~400 MB (FP16 variants)

---

## Asset Lifecycle



---

## ONNX Runtime Setup

```cpp
// Global environment (singleton)
static Ort::Env g_ort_env(ORT_LOGGING_LEVEL_WARNING, "Vanta");

// Session options
Ort::SessionOptions session_options;
session_options.SetIntraOpNumThreads(4);      // CPU threads
session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

// Load model
Ort::Session session(g_ort_env, model_path, session_options);
```

---

## Model Loading Order



---

## CLIP Inference

### Image Encoder


### Text Encoder


---

## Face Model Inference

### Detection


### Recognition


---

## NER Inference



---

## Quantization

Current models use **FP16** (half-precision floats) to reduce size by ~50% vs FP32 with minimal accuracy loss.

| Precision | Size | Speed | Accuracy |
|-----------|------|-------|----------|
| FP32 | ~600 MB | Baseline | 100% |
| FP16 | ~400 MB | 1.0-1.2x | ~99.5% |
| INT8 | ~200 MB | 1.5-2.0x | ~98% |

**Future:** INT8 quantization with ONNX Runtime's quantization tools for faster inference.

---

## Model Conversion Pipeline



---

## Memory Management

| Model | Load Memory | Inference Memory | Peak |
|-------|-------------|------------------|------|
| CLIP Image | ~170 MB | +50 MB | ~220 MB |
| CLIP Text | ~120 MB | +30 MB | ~150 MB |
| Face Det | ~4 MB | +10 MB | ~14 MB |
| Face Rec | ~44 MB | +20 MB | ~64 MB |
| NER | ~66 MB | +15 MB | ~81 MB |
| **Total** | **~400 MB** | **~125 MB** | **~530 MB** |

> **Note:** Models are loaded on-demand. CLIP Image + Face are loaded during indexing. CLIP Text + NER are loaded during search. Peak memory occurs during indexing.

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Model file missing | Log error, return empty results |
| Corrupted model | ONNX Runtime throws, caught and logged |
| Out of memory | Graceful degradation, skip batch |
| Wrong input shape | Assertion failure, log and skip |
