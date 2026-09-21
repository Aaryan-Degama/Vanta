# 28 — Cheat Sheet

---

## The stack, in one table

| Layer | Technology | Note |
|---|---|---|
| UI | React Native 0.85.3, React 19.2.3, TypeScript 6, Expo SDK 56 | 6 screens |
| Native module | Expo Modules API, Kotlin | 20 `AsyncFunction`s |
| Bridge | Hand-written JNI | 20 entry points, 11 return JSON strings |
| Engine | C++20, ~4,500 lines | CMake + NDK, **arm64-v8a only** |
| Inference | **ONNX Runtime 1.26.0**, CPU only | 27 MB prebuilt aarch64 |
| Vision | OpenCV (core, imgproc, imgcodecs) | |
| Storage | SQLite amalgamation + **sqlite-vec**, compiled in, WAL | one file, one transaction |

## The models — memorise these

| Model | Exact identity | Params | Size | Precision | I/O |
|---|---|---|---|---|---|
| **CLIP image** | OpenAI **ViT-B/32** `model.visual` | ~88 M | ~176 MB | **FP16** | 224×224×3 → **512-d** |
| **CLIP text** | OpenAI ViT-B/32 `encode_text` | ~63 M | ~127 MB | **FP16** | 77 tokens → 512-d |
| **Face detect** | **SCRFD-10GF** (`det_10g`, buffalo_l) | **4,226,183** | **4.11 MB** | **INT8 dynamic** | 640×640 → 9 outputs |
| **Face embed** | **ResNet50 + ArcFace** (`w600k_r50`, buffalo_l) | **43,591,296** | **41.76 MB** | **INT8 dynamic** | 112×112 → **512-d** |
| **NER** | **BERT-tiny**, `BertForTokenClassification`, **L=2, H=128** | **4,370,424** | **16.94 MB** | FP32 | [1,32] ×3 → [1,32,**7**] |

> ⚠️ README says "DistilBERT" and "RetinaFace" and "all FP16". All three are wrong. Know the truth.

## Dimensions & constants

| Thing | Value |
|---|---|
| CLIP embedding | **512-d** float32 stored (2,048 B) |
| Face embedding | **512-d** float32 stored (2,048 B) |
| CLIP input | 224×224, mean (0.48145466, 0.4578275, 0.40821073), std (0.26862954, 0.26130258, 0.27577711) |
| Face detect input | 640×640, (x−127.5)/128 |
| Face embed input | 112×112, (x−127.5)/127.5, **BGR→RGB** |
| CLIP text tokens | 77, BOS 49406, EOS 49407 |
| NER sequence | 32, CLS 101, SEP 102, PAD 0, UNK 100, vocab 30,522 |
| SCRFD anchors | 12800 / 3200 / 800 at strides 8 / 16 / 32, **2 per location** |
| Detection conf / NMS IoU | 0.5 / 0.4 |
| Indexing batch | 10 images |
| Search `top_k` | 30 |
| Burst window | 30 s |

## The clustering thresholds — and the conversion

$$\cos = 1 - \frac{L_2^2}{2} \qquad\text{(unit vectors; sqlite-vec default metric is \textbf{L2})}$$

| Constant | L2 | **cosine** | Meaning |
|---|---|---|---|
| `GOOD_THRESHOLD` | 0.60 | **0.82** | assign a confident face |
| `BLURRY_THRESHOLD` | 0.75 | **0.72** | assign a weak face (`det_score < 0.5`) |
| `MERGE_THRESHOLD` | 1.00 | **0.50** | merge two identities |

🔴 **Merging (0.50) is looser than assigning (0.82) — and merging is irreversible. They can't both be right.**

## The schema

```
files(id, abs_path UNIQUE, display_name, filetype, mime_type, size_bytes, mtime_unix,
      last_indexed_at, width_px, height_px, duration_ms, face_count, status, retry_count)
clip_vec         vec0(file_id PK, embedding FLOAT[512])
entities(entity_id PK, entity_type='person', display_name, relation, age, location,
         confidence, sample_count, created_at, updated_at)
person_centroids vec0(entity_id PK, embedding FLOAT[512])
face_detections(id PK, file_id→files, entity_id→entities, bbox_*, det_score, aligned_crop_path)
face_vec         vec0(detection_id PK, embedding FLOAT[512])
entity_memberships(entity_id, file_id, score DEFAULT 1.0, created_at)   ← score NEVER written
entity_relations(entity_a, entity_b, co_occurrence_count, ..., CHECK(entity_a < entity_b))
entity_relation_files(entity_a, entity_b, file_id)                      ← the O(1) 2-person index
```
`PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;` · **no `busy_timeout` anywhere** ·
`init_face_schema` creates **zero indexes**

## The search pipeline

```
typo correct → NER (SELF/PERSON/RELATION) → resolve (Damerau-Levenshtein ≤ 2)
  → residual → IntentBuilder → CLIP caption
  → candidates:  1 entity → entity_memberships
                 2 entities → entity_relation_files (O(1) PK probe)
                 3+ → intersect, smallest set first
                 empty → UNION fallback (relaxes the constraint)
                 no entities → global brute-force KNN
  → rerank by cosine(text, image) → verify faces → burst dedup → top 30
```

## Storage math (100k photos, 1.5 faces each)

```
CLIP vectors      100,000 × 512 × 4 B ≈ 195 MB
Face vectors      150,000 × 512 × 4 B ≈ 293 MB
relational + idx                      ≈  56 MB
                            DB TOTAL  ≈ 544 MB
Face crops        150,000 × ~30 KB    ≈ 4.5 GB   ← 8× the database, for avatars
```

## Verified findings — the twelve

| # | Finding | Severity |
|---|---|---|
| 1 | README claims document indexing; **none exists** | 🔴 |
| 2 | "DistilBERT" is **BERT-tiny** (2 layers, 4.4M params) | 🔴 |
| 3 | ~280 ms latency has **no harness** behind it | 🔴 |
| 4 | IntentBuilder **drops unmapped words** ("my dog at the beach" → "at a beach") | 🔴 |
| 5 | iOS is a **15-line stub**; Android arm64-only | 🔴 |
| 6 | `entity_memberships.score` **never written** → always 1.0 → people-only ranking unordered | 🔴 |
| 7 | Merge pass **cascades away** graph edges instead of transferring them | 🟠 |
| 8 | `entity_id = -1` tombstone **rejected by a foreign key**, silently | 🟠 |
| 9 | **Data race** on the tokenizer load; **use-after-free** on `g_analyzer` | 🟠 |
| 10 | Docs say FP16 everywhere; face models are **INT8** | 🟠 |
| 11 | **No `busy_timeout`** → immediate `SQLITE_BUSY` on writer contention | 🟠 |
| 12 | CI **has never run** (wrong directory + gitignored build dir) | 🟠 |

Plus the two silent accuracy bugs: **CLIP aspect-ratio squash** and **missing BGR→RGB in detection**.

## Top bottlenecks

1. Double image decode (~10–25% of indexing)
2. Batch size 1 despite dynamic batch dims (1.5–2.5× available)
3. Missing `face_detections` indexes (full scans on the search path)
4. 320px face crop per face (~4.5 GB)
5. Hand-written 1.2M-iteration preprocessing loops
6. CLIP text encoder on every search, never cached

## What to fix, in order

**Preprocessing bugs → evaluation harness → exemplar galleries + quality gating → indexing throughput → MobileCLIP.**
Not "more ML."

---

# THE 25 THINGS I MUST BE ABLE TO EXPLAIN WITHOUT HESITATION

1. **Why CLIP cannot recognise a specific person** — training signal, the objective actively opposing it, and the geometry. *(→ [09](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md))*
2. **The contrastive objective**, and why an image vector and a text vector are comparable at all.
3. **Identity is a filter, semantics is a ranker** — and why filtering first is both more correct and 10³× cheaper.
4. **Why a vector dot product cannot express a conjunctive set constraint** — or negation.
5. **ArcFace's additive angular margin**: $\cos(\theta_y + m)$, why it lowers the target logit, and why that produces intra-class compactness and inter-class separation.
6. **Why the angular margin makes open-set clustering possible** — cosine similarity is meaningful for identities the model never saw.
7. **Why a single centroid destroys a multi-modal identity** — it lands between the modes, too far from the right faces and too close to the wrong ones.
8. **That your "centroid" is actually a normalised EMA** (0.9/0.1), and the three consequences: order dependence, recency bias, `sample_count` unused.
9. **The exemplar-gallery alternative** (Apple's canonical exemplars), and why it's a schema change not an architecture change.
10. **$\cos = 1 - L_2^2/2$** — and therefore that your merge threshold (0.50) is looser than your assign threshold (0.82).
11. **Why a fixed global threshold is insufficient**, and the label-free calibration: co-occurring faces are free known-impostor pairs.
12. **Why two faces in one photo must not map to one person** — and why it hits siblings and parents hardest.
13. **The SCRFD decoder**: strides 8/16/32, 2 anchors per location, 12800/3200/800, distance-to-box regression, keypoint offsets, NMS, inverse letterbox.
14. **Why alignment uses `estimateAffinePartial2D`** — a 4-DoF similarity preserves facial geometry; a full affine would distort the identity signal.
15. **The two preprocessing bugs** — anisotropic 224×224 resize vs resize-short-side-and-crop; missing BGR→RGB where InsightFace uses `swapRB=True`.
16. **That the NER model is BERT-tiny, not DistilBERT** — 2 layers, H=128, 4.37M params — and that `token_type_ids` proves it.
17. **Why tiny is the right size** for a 4-class, ~10-word, near-templated task on the search critical path.
18. **BIO tagging and first-subword alignment**, and the actual label set (SELF / PERSON / RELATION only — no LOCATION).
19. **That GraphRAG inspired you but you do not implement it** — no LLM, no communities, no summaries, no generation, one hop.
20. **Why Android's SQLite forced a native engine** — it cannot load extensions, so sqlite-vec is impossible through it.
21. **Why sqlite-vec over FAISS** — one file, one transaction; FAISS is an index, not a database.
22. **Why WAL is the load-bearing pragma** — readers don't block the writer during a multi-hour index.
23. **Why JNI is crossed once per operation, not per item** — and that the real cost isn't the transition, it's losing transactions and warm native state.
24. **Why FP16 isn't automatically 2× faster** — bandwidth vs arithmetic, ARMv8.2 FP16 support, cast nodes, and your own 150,528 scalar conversions per image.
25. **That you have no evaluation harness** — and exactly how you'd build one, and that you ship three search toggles whose value you cannot currently measure.

---

## Knowledge classification

### MUST MEMORIZE (facts you cannot derive in the room)

- Model identities, parameter counts, sizes, precisions — the table at the top.
- Embedding dimension **512** for both CLIP and ArcFace; **224×224** and **112×112**; **77** and **32** token limits.
- The three thresholds in **both** L2 and cosine, and that merge < assign.
- SCRFD anchor counts **12800 / 3200 / 800** at strides **8 / 16 / 32**, **2 anchors per location**.
- NER labels: **O, B/I-SELF, B/I-PERSON, B/I-RELATION** — **7 labels, no LOCATION**.
- ONNX Runtime **1.26.0**, **CPU only**, **arm64-v8a only**.
- The table names, and that `entity_relation_files` is what makes 2-person queries O(1).
- Storage per vector: **2,048 bytes** (512 × 4).
- Your twelve verified findings.

### MUST UNDERSTAND (derive it, don't memorise it)

- The contrastive objective and why the shared space exists.
- The angular-margin argument and why it gives open-set transfer.
- The multi-modality/centroid geometry argument.
- $\cos = 1 - L_2^2/2$ and how to convert any threshold.
- Filter-then-rank, and the cost argument behind it.
- Why identity needs a separate index *and* a separate model.
- Transaction boundaries, WAL, and why virtual tables escape foreign keys.
- JNI reference lifetimes and why batching matters architecturally rather than for transition cost.
- The precision trade-offs — and why FP16 ≠ 2×.
- Why every threshold in the system should be adaptive rather than global.
- Brute force vs ANN, and why O(N) only bites the global path.

### NICE TO KNOW (bonus, don't lead with it)

- SigLIP's sigmoid loss and why it decouples from batch size.
- MagFace/AdaFace and feature norm as a free quality signal.
- Apple's $D_{ij} = \min(F_{ij}, \alpha F_{ij} + \beta T_{ij})$ and why the `min` matters.
- Apple's sparse-coding assignment — and why you'd *skip* it at your scale.
- Leiden/Louvain community detection over the co-occurrence graph.
- `GetPrimitiveArrayCritical` / direct `ByteBuffer` zero-copy.
- `SQLITE_CORE` and `sqlite3_auto_extension` mechanics.
- DiskANN / IVF / binary quantisation in sqlite-vec — and that the source is already in your `thirdparty/`.
- NIST FRVT demographic differentials, and Apple's 0.23 → 0.86 worst-subgroup result.

---

## Final reminders

- **Never quote a number you can't source.** Say "[estimated]" or "I don't know — here's how I'd measure it."
- **Never say "cross-platform."** Say "portable by construction, single-platform in practice."
- **Never say "we use GraphRAG."** Say "inspired by GraphRAG's architecture."
- **Never say "C++ for performance."** Say "sqlite-vec is unusable from Android's SQLite API."
- **Never say "DistilBERT."** Say "BERT-tiny, 2 layers, 4.4 million parameters."
- **Volunteer the centroid problem before you're asked.** It's your best answer, not your worst.
