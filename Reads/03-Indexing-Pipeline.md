# 03 — Indexing Pipeline

> How Vanta transforms raw media files into searchable embeddings and face clusters.

---

## Overview

The indexing pipeline runs as an **Android Foreground Service** (`IndexingService`) to survive app backgrounding. It processes every `pending` file in batches of 10, generating:

1. **CLIP image embeddings** — 512-dim visual-semantic vectors for semantic search
2. **Face detections + embeddings** — 512-dim ArcFace vectors for face recognition
3. **Face clusters** — grouped into person entities via online clustering
4. **Graph edges** — co-occurrence relationships between people in the same photo

![Vanta Logo](../vanta_logo.png)

---

## Step 1: Batch Loading

![Vanta Logo](../vanta_logo.png)

**Image preprocessing:**
- Load via `cv::imread(abs_path)`
- If either dimension > 1024, downscale proportionally
- CLIP expects 224×224 RGB; face models expect original resolution for detection

---

## Step 2: CLIP Image Embedding

![Vanta Logo](../vanta_logo.png)

**Model:** `clip_image_fp16.onnx` (OpenAI CLIP ViT-B/32, quantized to FP16)

**Output:** 512-dimensional unit vector stored in `sqlite-vec` virtual table:

```sql
CREATE VIRTUAL TABLE clip_vec USING vec0(
    file_id INTEGER PRIMARY KEY,
    embedding FLOAT[512]
);
```

---

## Step 3: Face Detection

![Vanta Logo](../vanta_logo.png)

**Detection model variants:**

| Variant | Model | Size | Speed | Accuracy |
|---------|-------|------|-------|----------|
| `buffalo_l` | `det_10g.onnx` | 4.3 MB | Medium | High (detects small faces) |
| `buffalo_sc` | `det_500m.onnx` | 2.5 MB | Fast | Medium |

**Output per face:**
```cpp
struct FaceResult {
    float bbox[4];        // x1, y1, x2, y2
    float det_score;      // confidence [0,1]
    float keypoints[10];  // 5 pairs of (x,y) landmarks
};
```

---

## Step 4: Face Alignment & Embedding

![Vanta Logo](../vanta_logo.png)

**Standard 5-point landmarks:**
```cpp
float kTargetLandmarks[5][2] = {
    {38.2946f, 51.6963f},   // left eye
    {73.5318f, 51.5014f},   // right eye
    {56.0252f, 71.7366f},   // nose
    {41.5493f, 92.3655f},   // left mouth
    {70.7299f, 92.2041f}    // right mouth
};
```

**Recognition model variants:**

| Variant | Model | Size | Embedding Dim |
|---------|-------|------|---------------|
| `buffalo_l` | `w600k_r50.onnx` | 43.8 MB | 512 |
| `buffalo_sc` | `w600k_mbf.onnx` | 13.6 MB | 512 |

---

## Step 5: Online Face Clustering

![Vanta Logo](../vanta_logo.png)

**Clustering algorithm:** Online single-pass with incremental centroid update

```cpp
const float CLUSTER_COSINE_THRESHOLD = 0.55f;

// Incremental centroid update
new_centroid = (old_centroid * sample_count + new_embedding) / (sample_count + 1);
```

**Why online?** New photos arrive incrementally. Re-clustering everything on every photo would be O(n²). Online clustering is O(n) per photo.

---

## Step 6: Graph Construction

After clustering, Vanta builds co-occurrence edges for every file with 2+ faces:

![Vanta Logo](../vanta_logo.png)

**Graph edge schema:**
```sql
CREATE TABLE entity_relation_files (
    entity_a INTEGER NOT NULL,
    entity_b INTEGER NOT NULL,
    file_id INTEGER NOT NULL,
    co_occurrence_count INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (entity_a, entity_b, file_id),
    FOREIGN KEY (entity_a) REFERENCES entities(entity_id),
    FOREIGN KEY (entity_b) REFERENCES entities(entity_id),
    FOREIGN KEY (file_id) REFERENCES files(id)
);
```

---

## Step 7: Post-Processing Passes

After all files are indexed, two cleanup passes run:

![Vanta Logo](../vanta_logo.png)

---

## Progress Tracking

The C++ layer exposes indexing progress via atomic globals:

```cpp
static std::atomic<int> g_index_processed_count(0);
static std::atomic<int> g_index_total_count(0);
static std::string g_current_processing_file = "";
static std::string g_index_status = "idle";  // idle | loading_models | processing | paused | finished
```

JavaScript polls `getIndexProgress()` to show a progress bar.

![Vanta Logo](../vanta_logo.png)

---

## Pause & Resume

![Vanta Logo](../vanta_logo.png)

> **Implementation:** `g_pause_embedding` is an `std::atomic<bool>`. The indexing loop checks it between batches.

---

## Performance Characteristics

| Operation | Time per image | Notes |
|-----------|---------------|-------|
| Image load + resize | ~5-10 ms | Depends on original size |
| CLIP embedding | ~50-100 ms | ONNX Runtime, CPU |
| Face detection | ~20-40 ms | `det_10g` |
| Face embedding (per face) | ~10-20 ms | `w600k_r50` |
| Clustering + graph | ~1-5 ms | SQLite inserts |
| **Total per image** | **~100-200 ms** | 1 face, 1024px image |
| **Throughput** | **~5-10 images/sec** | Single-threaded |

> **Optimization opportunity:** Batch CLIP inference (currently 1 image per run). Batch size 4-8 could improve throughput 2-3x.
