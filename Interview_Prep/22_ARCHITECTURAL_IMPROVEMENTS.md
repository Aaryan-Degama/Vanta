# 22 — Architectural Improvements & the Decision Table

## Part 1 — What I would redesign

### Tier 1 — Low-hanging fruit (hours to days, large payoff)

| # | Change | Where | Why | Effort |
|---|---|---|---|---|
| 1 | **Fix the CLIP aspect-ratio squash** | `CLIP_image.cpp::load_img` | `cv::resize(img, 224×224)` anisotropically distorts every photo. Reference CLIP resizes the short side then centre-crops. **Degrades 100% of the index** | 5 lines |
| 2 | **Add the missing `BGR→RGB` in detection** | `Seg_model.cpp::get_faces` | SCRFD was trained on RGB (`swapRB=True`); Vanta feeds BGR. Costs recall on exactly the marginal faces | 1 line (or replace the loop with `cv::dnn::blobFromImage`) |
| 3 | **Decode each image once** | `generateEmbeddingsNative` | `cv::imread` for CLIP, then `ifstream`+`imdecode` for faces. **[ESTIMATED]** 10–25% of indexing time and 2× transient memory | ~15 lines |
| 4 | **Write a real `entity_memberships.score`** | `face_DB.cpp` | The column is read by the ranker and never written → people-only search is unordered. The cosine is already in hand from the `MATCH` | 3 lines |
| 5 | **`sqlite3_busy_timeout(db, 5000)`** | every `sqlite3_open*` | Zero busy handling anywhere → immediate `SQLITE_BUSY` on writer contention | 1 line ×8 |
| 6 | **Add `face_detections` indexes** | `init_face_schema` | `init_face_schema` creates **no indexes**. `verify_entities_in_file` full-scans a 150k-row table per result, on the search path | 2 lines |
| 7 | **Don't let the Intent Builder drop words** | `IntentBuilder::buildIntent` | `"my dog at the beach"` → `"at a beach"`. Append the unmatched residue | ~5 lines |
| 8 | **Move the tokenizer load inside the mutex** | `vanta.cpp` | A real data race on concurrent cold-start searches | 2 lines |
| 9 | **`android:allowBackup="false"`** | app manifest | Otherwise the face database can reach Google Drive — contradicting the product's core promise | 1 line |
| 10 | **Stop writing a 320px crop per face** | `run_face_pipeline` | **~4.5 GB at 100k photos** for avatars, of which one per *entity* is ever shown. Generate lazily from the stored bbox | ~20 lines |
| 11 | **Replace `system("rm -rf …")`** | `resetFaceDataNative` | Not exploitable today, but the wrong construct. `std::filesystem::remove_all` | 3 lines |
| 12 | **Cache ORT input/output names** | `Seg_model.cpp`, `CLIP_model.cpp` | Re-queried per inference, and **per face** inside the embedding loop | ~10 lines |
| 13 | **Fix the README** | `README.md` | Remove the document-indexing claim; correct "DistilBERT" → BERT-tiny; qualify or drop the 280 ms figure | — |

> **These thirteen changes are perhaps two days of work and they fix two silent accuracy bugs, the biggest storage
> problem, the biggest search-latency problem, a data race, a security exposure, and three false claims.** Being able
> to name a list like this about your own project is one of the strongest signals you can give.

### Tier 2 — Medium engineering (days to weeks)

| # | Change | Why |
|---|---|---|
| 14 | **Quality scoring + gating** | [11 §Part 4](./11_FACE_CLUSTERING_AND_PERSON_SEGREGATION.md). Unlocks quality-weighted updates, better avatars, a real membership score, and adaptive thresholds — all from one addition |
| 15 | **Exemplar galleries instead of one centroid** | [21](./21_APPLE_PHOTOS_COMPARISON.md). A schema change, not an architecture change |
| 16 | **Invert the merge/assign thresholds; make merging conservative** | Today merging (cos 0.50) is looser than assignment (cos 0.82), and merging is the irreversible one |
| 17 | **Per-photo injective assignment (Hungarian)** | A person appears at most once per photo. Free, always-true constraint, currently ignored |
| 18 | **Co-occurrence as cannot-link** | The graph already stores "these two are definitely different people". Feed it back into clustering |
| 19 | **Batch ONNX inference (4–8)** | Both graphs already declare dynamic batch dimensions. **[ESTIMATED]** 1.5–2.5× indexing throughput |
| 20 | **Pipelined indexing threads** | Overlap decode / inference / DB. **[ESTIMATED]** 1.3–1.6× — not more, because ORT already saturates cores during inference |
| 21 | **One transaction per photo** | Per-photo work currently spans four transactions. Set `status='indexed'` last, inside it |
| 22 | **Check `sqlite3_step` return codes** | Discarded in ~15 places. This is *why* three bugs in this audit are invisible |
| 23 | **Deletion & re-index reconciliation** | Deleted photos live forever in the index; modified photos keep stale embeddings |
| 24 | **Evaluation harness** | Synthetic labelled library + 200 queries + Recall@10/NDCG@10 per query family. **Without this, every other improvement is unfalsifiable** |
| 25 | **Golden tests for all three preprocessing paths** | Would have caught #1 and #2 automatically |
| 26 | **Structured error envelope across JNI** | `"[]"` currently means both "no results" and "the model failed to load" |
| 27 | **Fix CI** | Move `.github/` to the repo root; run `expo prebuild`; cache the OpenCV SDK |
| 28 | **Thermal/battery-aware indexing** | `PowerManager.getCurrentThermalStatus()`, charging constraints, a throttled mode |

### Tier 3 — Research-level (weeks to months)

| # | Change | Why / Verdict |
|---|---|---|
| 29 | **MobileCLIP-S0 or S1** | ~11 M-param image encoder at ViT-B/16-class retrieval quality. Would cut indexing cost several-fold **and** shrink the APK by >150 MB. 🟢 **The best model-level change available** |
| 30 | **OpenCLIP LAION-2B ViT-B/32 weights** | Identical architecture, identical export path, identical latency, better retrieval. 🟢 **Free accuracy — do this before anything fancier** |
| 31 | **MagFace / AdaFace recogniser** | The pre-normalisation feature norm *is* a quality score, for free |
| 32 | **Upper-body embeddings gated by moments** | Apple's approach. Real recall gain on turned-away people |
| 33 | **Moments / event nodes** | Enables "that trip last summer" and gates torso matching. Cheap — `mtime_unix` is already indexed |
| 34 | **Community detection (Louvain) over co-occurrence** | "photos with my college friends". Milliseconds at this scale. The one genuinely GraphRAG-shaped idea that fits |
| 35 | **ANN index (DiskANN/IVF)** | The source is **already in `thirdparty/`** and simply isn't in the CMake list. Only needed past ~100k vectors |
| 36 | **Binary quantisation + rescoring for `clip_vec`** | ~8–32× faster global KNN, 94% less vector storage, minimal recall loss with float rescoring |
| 37 | **Score fusion** | $\alpha\cos + \beta\min_i m_{i,f} + \gamma r(f) + \delta q_f$. **Only after #24** — untunable without measurement |
| 38 | **Document & video indexing** | Currently claimed in the README and entirely absent. Video needs keyframe extraction; documents need text extraction + a text embedder (the `DBschema.sql` design assumed bge-small at 384-d) |
| 39 | **On-device LLM for query understanding** | ~1 GB, seconds per query. ❌ Wrong trade for search latency today |
| 40 | **GNN / graph embeddings** | ❌ **Explicitly reject.** A few hundred nodes and one edge type. Nothing for a GNN to learn that SQL doesn't give exactly. *Knowing when not to add ML is a stronger signal than knowing how to add it* |

---

## Part 2 — The person-recognition progression

The exact chain to be able to recite:

```
Current system
  single EMA "centroid" per person, no quality filter,
  assign at cos 0.82, merge at cos 0.50, k=1, no constraints
        ↓
quality-aware embeddings
  composite Q from size/sharpness/pose/exposure; gate at Q_MIN;
  weight the representation update by Q; use Q for avatars and membership scores
        ↓
multiple exemplars
  person_centroids → person_exemplars (k ≤ 10, quality- and diversity-selected);
  score = max over exemplars, not distance to a mean
        ↓
conservative clustering
  invert the thresholds so merging is STRICTER than assigning;
  require N ≥ 3 mutually-consistent faces to form a new identity;
  add a second-best ratio test so the system can decline to decide
        ↓
hierarchical clustering
  pass 1: high-precision within moments;  pass 2: median-linkage HAC across moments
        ↓
unknown rejection
  "unknown" becomes a first-class terminal state rather than a forced guess
        ↓
upper-body / context evidence
  D = min(F, αF + βT), torso compared only within a moment
        ↓
incremental identity maintenance
  merges are logged with provenance and are reversible;
  graph edges transfer on merge; thresholds recalibrate as the library grows
        ↓
user feedback
  "same person" / "not this person" → permanent must-link / cannot-link constraints
  that override every algorithmic decision
```

---

## Part 3 — The decision table

| Decision | Chosen | Why | Alternatives | Why not | Trade-off accepted |
|---|---|---|---|---|---|
| **UI framework** | React Native | Fast iteration on the part of the system that changed most; ~0% of compute is in the UI | Native Views, **Jetpack Compose**, Flutter, KMP, PWA | Compose is the strongest counterfactual and, since iOS stayed a stub, arguably the better choice. Flutter adds a language without removing one. PWA can't reach MediaStore or JNI | JS-thread overhead on a path where it doesn't matter |
| **Toolchain** | Expo (prebuild) | Expo Modules API removes most native-module boilerplate; autolinking; prebuild leaves full native access | Plain RN | Strictly more boilerplate for the same result | Expo Go unusable; full rebuild per native change |
| **UI language** | TypeScript | Type safety across 6 screens and 20 native calls | JS | No types at a boundary that's already untyped (JSON strings) | Build step |
| **Android language** | Kotlin | Null safety across JNI, data classes, `Sequence`, `use{}`, the Android/Expo default. **Not for speed** | Java | Verbose; fights an Expo Modules API that is Kotlin-first | None meaningful |
| **Native language** | C++20 | **sqlite-vec is impossible from Android's SQLite API**; ORT and OpenCV are C/C++; memory control in a multi-hour loop | Kotlin, **Rust**, Swift, JS/WASM | Rust is the serious alternative and would have statically prevented most of my real bugs — but a 3-week solo build and an NDK toolchain argued against it | No memory safety: 2 UAF/race paths, ~15 unchecked return codes, arm64-only |
| **Bridge** | JNI, hand-written | Only way from JVM to a custom `.so`. Batched to **O(1) crossings per operation**, not per item | JNI via a generator, JSI/TurboModules | Expo already provides the JS-side plumbing; the native side must be JNI regardless | Verbose; no exception checks; untyped JSON contract |
| **Inference runtime** | ONNX Runtime 1.26.0 | **All three models are natively available as ONNX.** InsightFace ships `.onnx` directly; CLIP exports in 20 lines. One runtime, zero custom converters, C++ API | TFLite, ExecuTorch, NCNN, MNN, MediaPipe, Core ML | Each needs a per-model conversion I'd own the risk for. TFLite is otherwise excellent on Android | 27 MB binary; CPU-only as configured |
| **Execution provider** | CPU | Deterministic across every device — which matters because embedding values decide identity merges | NNAPI, XNNPACK, QNN, GPU | NNAPI is vendor-inconsistent and deprecated; QNN is vendor-specific | Leaving real speed on the table. **XNNPACK should be tried — still CPU, still deterministic** |
| **Image-text model** | CLIP ViT-B/32 | Open vocabulary; trivial ONNX export; huge ecosystem | OpenCLIP, SigLIP/SigLIP 2, MobileCLIP, EVA-CLIP, ALIGN | Optimised for a working end-to-end pipeline first. **MobileCLIP-S0 is what I'd choose today**; OpenCLIP weights are a free drop-in upgrade | Weakest of the modern options; 32×32 patches lose fine detail |
| **CLIP precision** | FP16 | **Size** — halves ~600 MB to ~300 MB, my binding constraint | FP32, INT8 | INT8 on a ViT is delicate (attention softmax, LayerNorm) and I have no calibration data by design | Never benchmarked for latency; the FP32→FP16 input conversion is a 150k-element scalar loop |
| **Face models** | InsightFace `buffalo_l` (SCRFD-10GF + ResNet50 ArcFace) | Recall on small/non-frontal faces; a missed detection is unrecoverable. Strong embeddings matter because clustering is unsupervised | `buffalo_sc` (det_500m + MobileFaceNet) | ~20× cheaper detection but misses the small faces that dominate real libraries | ~46 MB and 5–10× the per-image cost |
| **Face precision** | INT8 dynamic (QUInt8) | Convnets quantise well; `quantize_dynamic` needs **no calibration data**, which I can't collect by design | FP32, FP16, static INT8 | Static INT8 needs representative data | **Accuracy cost unmeasured** — the first evaluation I'd add |
| **Recognition loss** | ArcFace | Additive angular margin ⇒ geodesically uniform margin, intra-class compactness, inter-class separation, and cosine similarity that is **meaningful for unseen identities** | FaceNet, SphereFace, CosFace, CurricularFace, **MagFace, AdaFace** | MagFace/AdaFace would give a **free quality signal** from the feature norm — directly addresses my biggest gap | Pretrained; I don't control the training data or its demographics |
| **Query NER** | BERT-tiny (2 layers, H=128, 4.37 M params) | On the **search critical path**; ships in the APK; the task is narrow and near-templated | DistilBERT, BERT-base, regex, gazetteer | ~15× larger for a 4-class, 10-token task. Regex can't disambiguate "rose" the person from "rose" the flower | **No held-out F1 measurement.** Risk on unusual phrasings |
| **Database** | SQLite (own amalgamation) + sqlite-vec, WAL | **Android's SQLite cannot load extensions**, so `vec0` forced compiling my own. Vectors and relations in **one file, one transaction** | Postgres/MySQL, Room, Realm, DuckDB, RocksDB | Client-server is undeployable; Room blocks extensions; KV stores mean hand-building every index and join | Brute-force KNN only |
| **Vector search** | sqlite-vec `vec0`, exhaustive, L2 | Transactional integrity with the relational data beats raw ANN throughput at 10–100k vectors | FAISS, Chroma, Qdrant, Milvus, **Pinecone/Weaviate** | FAISS is an index, not a database — two stores, two durability models, manual reconciliation after every crash. **Cloud vector DBs violate the core product requirement** | O(N) global KNN. **DiskANN/IVF source is already in `thirdparty/`, uncompiled** |
| **Identity representation** | One EMA "centroid" per person | Simplest thing that works; O(1) storage and one KNN probe | Medoid, quality-weighted mean, **exemplar gallery**, sub-clusters, covariance | 🔴 **This is my worst decision.** A single vector can't represent a multi-modal identity | Fragmentation *and* false merges simultaneously |
| **Clustering** | Online nearest-centroid + relaxed second pass + greedy merge | Incremental, O(1) per face, no batch phase needed | Agglomerative, DBSCAN, HDBSCAN, Apple's two-pass | Online was chosen for incrementality, which is a real requirement — but it locks in early mistakes | Order-dependent; merge threshold is looser than assign |
| **Graph** | Relational tables + a materialised edge→file index | Hundreds of nodes, one edge type, 1-hop queries. SQL is exactly right | Neo4j, an embedded graph DB, GNN embeddings | Pure cost at this scale | No multi-hop, no communities, no typed edges |
| **GraphRAG** | **Inspiration only** | Took the *architecture* — extract entities, resolve to nodes, filter structurally, rank semantically | Literal GraphRAG | No LLM on device; no generation stage; no corpus-level questions | Must be explicit that it is not an implementation |
| **Reranking** | Graph = hard filter → CLIP = soft ranker | Each signal used where it's authoritative. Also 3 orders of magnitude cheaper than global KNN | CLIP-only, score fusion, a cross-encoder | Fusion is untunable without an eval harness I don't have | The graph's own confidence never enters ranking |
| **JNI batching** | O(1) crossings per operation | The loop, inference and DB all live natively. One call indexes 50,000 photos | Per-image calls | Would prevent transactions and warm native state | `startStoring` materialises the whole scan in JVM heap first |
| **Local inference** | 100% on device, no `INTERNET` permission | **The product requirement.** Architecturally enforced, not policy-enforced | Cloud, hybrid | Defeats the entire premise | Every downstream constraint in this table |
| **Background work** | Foreground `Service` | Survives backgrounding; runs a multi-minute blocking JNI call naturally | `WorkManager`, plain thread | WorkManager is the better long-term answer (reboot, Doze, constraints, backoff) but chunks the work | `START_STICKY` restart passes a null Intent, so it doesn't actually auto-resume |
| **Platform** | Android arm64-v8a only | Bundled ORT and OpenCV are arm64 | +armeabi-v7a, +x86_64, iOS | Multiplies APK size; **x86_64 absence means no emulator** | iOS is a 15-line stub. **Never say "cross-platform"** |

---

## Part 4 — Vanta v2, in one paragraph

> "If I rebuilt Vanta tomorrow, the shape would be the same — **the hybrid architecture is right** and I'd defend it:
> a structured identity index plus a semantic vector index, with a query planner that routes each part of a sentence
> to the index that can answer it. What I'd change is everything about the *quality* layer.
>
> I'd fix the two preprocessing bugs first, because they silently degrade every embedding I've ever computed and
> they cost nothing to fix. I'd build the evaluation harness second, because without it every subsequent decision is
> a guess — and I'd note that I currently ship three user-facing search toggles whose value I cannot measure.
> Then I'd replace the single-centroid identity representation with exemplar galleries and add a quality gate,
> which together address the deepest flaw in the system. I'd swap ViT-B/32 for MobileCLIP-S0, which is smaller,
> faster and better. And I'd make the indexer thermal-aware and 3–5× cheaper per image so a 100,000-photo library
> goes from fourteen hours to three.
>
> The thing I would **not** do is add more machine learning. Vanta's problems aren't a shortage of models — they're
> an unvalidated representation choice, two preprocessing bugs, a missing index, and no way to measure any of it."
