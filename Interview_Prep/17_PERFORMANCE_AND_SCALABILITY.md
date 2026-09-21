# 17 — Performance, Memory, Battery & Scalability

> **Ground rule for this document and for your interview: never invent a measurement.**
> Vanta's repository contains **no benchmark harness, no recorded results, and no profiling output**.
> `vanta_logs.txt` is 561 lines of Android system logcat from an app install/launch and contains **zero**
> `VantaEngine` or `VantaQueryEngine` lines. Everything below is labelled accordingly.

---

## 1. The README's latency claim

> *"Average end-to-end search latency of ~280ms across all query combinations … with rare peaks of ~430ms-450ms."*
> — `README.md`

**Status: [UNKNOWN / NEEDS BENCHMARK].**

What *is* true: the engine times itself. `search_images` brackets the whole pipeline with
`std::chrono::high_resolution_clock` and logs `"Search complete: %zu results in %lld ms"`. So the number was almost
certainly read off logcat on a real device — it is not fabricated from nothing. But:

- there is no harness, no query set, and no recorded output in the repo;
- "average across all query combinations" implies a methodology that isn't described anywhere;
- the two code paths (graph-restricted vs global KNN) have *completely different* cost profiles, and averaging them
  hides that.

> **How to answer:** *"That number came from reading my own timing logs on my device during development — the engine
> instruments itself with `std::chrono` around the full pipeline. But I don't have a reproducible harness or a
> recorded dataset behind it, so I'd treat it as an informal observation, not a benchmark. If you want a number I'd
> defend, I'd need to build a fixed query set and report p50/p95 per path on a named device. I'd also stop averaging
> the two paths, because entity-restricted search and global KNN scale completely differently — one is O(candidates)
> and the other is O(library)."*
>
> That answer is **stronger** than a confident 280 ms, because it demonstrates you know what a benchmark is.

---

## 2. Search latency — stage by stage

| Stage | Cost model | **[ESTIMATED]** | Notes |
|---|---|---|---|
| JS → Expo → Kotlin | 2 string marshals + thread spawn | 1–5 ms | |
| `extractAssetsIfNeeded` (per search!) | 9 × `File.exists()` | <1 ms warm | Cheap, but it's redundant IO on the search path |
| JNI in | 2 × `GetStringUTFChars` | <0.1 ms | |
| **Typo correction** | delete-neighbourhood probes + Damerau-Levenshtein over a ~40+N word dict | 1–3 ms | `get_deletes` is O(len²) per word; trivial at query length |
| **NER inference** | BERT-tiny, 2 layers, H=128, seq 32, 1 thread | **5–20 ms** | ~10 M FLOPs. `Reads/05` says 20–50 ms and calls it DistilBERT; at 4.4 M params it should be **faster** than that |
| **Entity resolution** | `get_all_person_entities` (full table scan of `entities`) + Damerau-Levenshtein per candidate | 1–5 ms | O(E × L²). Fine at E ≤ 1000 |
| **Candidate lookup** | 1–k indexed SQL lookups + set intersection | 1–20 ms | Scales with the *number of photos containing that person*, not library size |
| **CLIP text embedding** | 12-layer transformer, width 512, 77 tokens, FP16 | **30–80 ms** | 🔴 **The dominant search cost.** Fixed, independent of library size |
| **Reranking (restricted)** | \|cand\| × 512 scalar MACs + a `rowid` lookup per candidate | 2–15 ms | The per-candidate SQL point lookup probably costs more than the arithmetic |
| **Reranking (global KNN)** | brute force N × 512 | **~1 ms/1k vectors** | 100k ⇒ ~100 ms; 500k ⇒ ~500 ms |
| **Face verification** | one query per result over `face_detections` | 🔴 **grows with library** | No index on `face_detections(file_id)` — see [15](./15_SQLITE_AND_SQLITE_VEC.md). Full scan × 30 results |
| Burst dedup | O(n²), n ≤ 30 | <1 ms | |
| JSON build + `NewStringUTF` | string concat, ~30 rows | <1 ms | |
| JSON.parse + render | JS | 5–20 ms | |

**The shape to remember:**

| Path | Dominant cost | Scales with library? |
|---|---|---|
| **Entity-restricted** | CLIP text encoder (fixed ~30–80 ms) + face verification | ⚠️ only via the missing `face_detections` index |
| **Global KNN** | brute-force scan, ~1 ms per 1,000 vectors | 🔴 **linearly** |

**The most important insight, and a great thing to volunteer:** *"the CLIP text encoder is a fixed cost I pay on
almost every query, and it's the biggest single item. Two obvious wins: I never cache text embeddings — the same
query typed twice re-runs the whole transformer — and for a people-only query like 'me and mom' I build a CLIP
caption of `""` and correctly skip the encoder entirely, which is already the right behaviour. An LRU cache of
query→embedding would make repeat searches nearly free."*

---

## 3. Memory

### Why model disk size ≠ RAM footprint

Five distinct reasons, and being able to list them is the answer:

1. **`mmap` vs copy.** ORT can memory-map weights; those pages are clean, file-backed, and reclaimable under
   pressure. They show in VSS/PSS but aren't "real" pressure the way anonymous memory is.
2. **Quantised weights dequantise into activations.** `w600k_r50.onnx` is 41.76 MB on disk as INT8, but
   `DynamicQuantizeLinear` + `ConvInteger` produce **int32 accumulators and float activations** at runtime. The
   activation memory is unrelated to the weight size.
3. **Activations are the other half.** A ViT-B/32 forward pass holds 50 tokens × 768 dims × 12 layers of
   intermediate tensors. SCRFD at 640×640 holds feature maps at strides 8/16/32 — the stride-8 map alone is
   80×80×C.
4. **ORT's arena allocator never gives memory back.** `OrtArenaAllocator` pre-reserves large blocks and sub-allocates.
   Peak RSS is the **high-water mark**, and it persists for the session's lifetime.
5. **Graph optimisation can duplicate weights** — fused nodes may hold transformed copies of the originals.

### Rough budget

| Component | Disk | **[ESTIMATED]** resident during indexing |
|---|---|---|
| CLIP image FP16 | ~176 MB | ~176 MB weights + 30–60 MB activations |
| CLIP text FP16 | ~127 MB | loaded on search, not indexing |
| SCRFD INT8 | 4.11 MB | ~4 MB + 20–50 MB activations (640×640 feature maps are large) |
| ArcFace INT8 | 41.76 MB | ~42 MB + 10–20 MB |
| NER FP32 | 16.94 MB | ~17 MB + <5 MB (seq 32) |
| OpenCV decode buffers | — | **2× full-resolution `cv::Mat`** — a 12 MP image is 12 M × 3 B = **36 MB each**, and Vanta decodes twice |
| SQLite page cache + WAL | — | tens of MB |
| RN / Hermes / JVM | — | 80–150 MB baseline |

**[ESTIMATED] peak during indexing: ~450–650 MB.** On a 4 GB device with a typical per-app limit that is
uncomfortable but survivable; the decode buffers are the volatile part.

**Three concrete reductions:**
1. **Decode once** — halves the largest transient allocation ([16](./16_INDEXING_PIPELINE.md)).
2. **Call `unload()`** on CLIP-image and the face models when `IndexingService` stops. Both classes implement it;
   nothing ever calls it, so the models stay resident for the process lifetime.
3. **Downscale before decode.** `cv::IMREAD_REDUCED_COLOR_2/4` decodes a JPEG at 1/2 or 1/4 resolution *during*
   decode — much faster and much smaller. Since CLIP wants 224×224 and SCRFD wants 640×640, decoding a 12 MP image
   at full resolution is pure waste. **[ESTIMATED]** this alone could cut decode time and peak memory by ~4×.
   *(Caveat: reduced decode lowers the effective resolution of small faces, so it trades detection recall for speed
   — measure before shipping.)*

---

## 4. Battery

**[UNKNOWN]. Do not quote a percentage.** What you can say is the physics and the measurement plan.

### Why this workload is the worst case for a phone

| Factor | Vanta's behaviour |
|---|---|
| **Sustained CPU** | Hours at near-100% on the big cores. Phones are designed for bursty loads with idle gaps |
| **DVFS** | Dynamic power ≈ $C V^2 f$, and $V$ rises with $f$. **Energy per image is worse at peak clocks**, so "finish faster to save battery" is false here |
| **Thermal throttling** | Sustained load heats the SoC past its trip points; the kernel drops frequency. Throughput falls *and* the work still costs energy |
| **DRAM traffic** | Decoding 12 MP images and streaming model weights is memory-bandwidth heavy, and DRAM is a significant power draw independent of the CPU |
| **Storage IO** | Reading every photo twice + writing ~30 KB of crop JPEG per face |
| **Screen-off background work** | A foreground service with a notification keeps the device from deep-idling |

### How to measure it properly

```bash
adb shell dumpsys batterystats --reset
# run indexing for a fixed corpus / fixed duration
adb shell dumpsys batterystats > stats.txt
# then: https://github.com/google/battery-historian  → per-UID attributed energy
```

| Tool | What it answers |
|---|---|
| `dumpsys batterystats` + **Battery Historian** | mAh attributed to your UID, wakelocks, CPU time by process |
| **Perfetto** | correlated CPU frequency, thermal zone temperature, scheduler state over the whole run |
| `dumpsys thermalservice` | when throttling engaged and at what level |
| **Android Studio Energy Profiler** | coarse but immediate |
| **Hardware power monitor** (Monsoon / a smart plug for charge-rate) | the only ground truth |

**Methodology that makes the number meaningful:** fixed corpus, fixed device, airplane mode, screen off, identical
starting battery and temperature, ≥3 runs, report mAh per 1,000 photos — not a percentage, since percentage depends
on battery capacity.

The battery-conscious indexing design is in [16](./16_INDEXING_PIPELINE.md).

---

## 5. Scalability — where it breaks, in order

| # | Wall | Threshold **[ESTIMATED]** | Symptom | Fix |
|---|---|---|---|---|
| 1 | **Face-crop disk usage** | ~50,000 photos (~2 GB) | User storage fills. 4.5 GB at 100k — ~8× the database | Generate avatars lazily; the bbox and path are already stored |
| 2 | **Indexing wall-clock** | ~50,000 photos (~7 h) | Unusable cold start | Decode once, batch inference, pipeline threads → 3–5× |
| 3 | **Missing `face_detections` indexes** | ~50,000 photos | `verify_entities_in_file` full-scans a 150k-row table, once per result, on the search path | `CREATE INDEX` × 2. **One-line fix, largest search-latency win** |
| 4 | **`merge_similar_entities` O(n²)** | ~5,000 entities | 12.5 M 512-d L2 computations, plus re-preparing 7 SQL statements inside the inner loop | Use `person_centroids` KNN to get candidate pairs instead of all pairs; hoist the statements |
| 5 | **Brute-force global KNN** | ~100k–500k vectors | Global semantic search gets slow (linear) | Compile in sqlite-vec's DiskANN/IVF (**already in `thirdparty/`**), or binary-quantise + rescore |
| 6 | **JVM heap in `startStoring`** | ~200,000 files | OOM materialising the whole scan before one JNI call | Use the 200-file batching `scanPhone` already provides |
| 7 | **DB size** | ~500k photos (~2.7 GB) | Storage pressure | float16 / int8 vectors → 50–75% saving |
| 8 | **Clustering quality** | 🔴 **degrades from photo #1** | Not a scaling wall — a *correctness* ceiling that worsens with more people and more appearance variation | [11](./11_FACE_CLUSTERING_AND_PERSON_SEGREGATION.md) |
| — | SQLite file limit | 281 TB | never | — |

> **The important reframing:** *"Most of my walls are storage and throughput, and they're all fixable with known
> engineering. The one that isn't a scaling problem at all is clustering quality — it's bad at 1,000 photos and
> it's bad at 100,000, just more visibly. **That's the thing I'd actually fix first**, because a faster system that
> puts your aunt in your mother's album isn't better."*

---

## 6. Critical bottlenecks — ranked

| Rank | Bottleneck | Where | Cause | Evidence | Impact | Mitigation |
|---|---|---|---|---|---|---|
| 1 | **Double image decode** | `generateEmbeddingsNative` | `cv::imread` in `CLIP_instance` **and** `ifstream`+`imdecode` in `run_face_pipeline` | both calls visible in the same loop body | **[ESTIMATED]** 10–25% of indexing time; 2× transient memory | Decode once, pass the `cv::Mat` |
| 2 | **Batch size 1** | CLIP + ArcFace | `input_dims = {1,…}` although both graphs declare dynamic batch | ONNX graph inputs `[batch_size,3,224,224]`, `[None,3,112,112]` | **[ESTIMATED]** 1.5–2.5× throughput left on the table | Batch 4–8 |
| 3 | **Missing `face_detections` indexes** | search + indexing | `init_face_schema` creates **zero** indexes | source | Full scans on the search path | 2 × `CREATE INDEX` |
| 4 | **Face crops written per face** | `run_face_pipeline` | `align_face(…,320)` + `cv::imwrite` for every detection | source | ~4.5 GB @ 100k, plus encode+write cost per face | Lazy generation |
| 5 | **Hand-written preprocessing loops** | `Seg_model::get_faces` | 1.2 M `at<cv::Vec3f>()` with a cache-hostile stride | source | **[ESTIMATED]** 10–30 ms/image | `cv::dnn::blobFromImage` (also fixes the BGR/RGB bug) |
| 6 | **CLIP text encoder on every search** | `search_images` | No caching | source | 30–80 ms per query, repeated for identical queries | LRU cache keyed on the caption |
| 7 | **Scalar FP32→FP16 conversion** | `CLIP_session::get_embedding` | 150,528 scalar converts/image | source | **[ESTIMATED]** 5–15 ms/image | Vectorise, or export an FP32-input model |
| 8 | **Single-threaded pipeline** | indexing | No overlap between decode / inference / DB | source | **[ESTIMATED]** 1.3–1.6× available (not 3×, since ORT already uses all cores during inference) | Bounded-queue pipeline |
| 9 | **Model load on first search** | `searchImagesNative` | Lazy load of CLIP + tokenizer + NER | source | **[ESTIMATED]** multi-second first-search latency | Warm up in `OnCreate` on a background thread |
| 10 | **`merge_similar_entities` O(n²)** | end of indexing | all-pairs centroid comparison | source | grows quadratically | KNN-candidate pairs only |
| 11 | **ORT threads unbounded on CLIP/face** | sessions | default = core count | source | Peak power and thermal throttling | `SetIntraOpNumThreads` + a battery mode |
| 12 | **`getIndexProgress` polling** | JS | timer-driven JNI call | source | Wakes the JS thread during a multi-hour job | Expo `EventEmitter` push, or back off the interval |

---

## 7. Mobile hardware reality

| Resource | Vanta's use | Notes |
|---|---|---|
| **CPU** | **100% of all inference** | ORT CPU EP with MLAS ARM NEON kernels. No GPU, no NPU |
| **GPU** | unused | ORT has an NNAPI EP available; the header is in `thirdparty/` and is never included |
| **NPU / DSP** | unused | QNN (Hexagon) would suit the quantised convnets well, but it's vendor-specific and needs a different ORT build |
| **RAM** | ~450–650 MB peak **[ESTIMATED]** | Decode buffers are the volatile component |
| **Storage** | DB + crops + models | Crops dominate — see above |
| **Thermals** | unmanaged | No `PowerManager` thermal listener anywhere |
| **Battery** | unmanaged | No charging/level constraints |

> **On not using the GPU/NPU — the defensible version:** *"I made a deliberate choice to stay on CPU, and I'd
> defend it for a v1: the CPU EP is the only backend that is guaranteed to produce identical results on every
> device. NNAPI's behaviour varies enormously by vendor, ops silently fall back to CPU, and for quantised models
> some drivers change numerics. For a system where a face embedding's *exact* value determines whether two people
> are merged, non-determinism across devices is a real correctness hazard, not just a performance question. That
> said, 'I didn't measure it' is the weaker half of the answer — the right next step is to try XNNPACK first, since
> it's still CPU and therefore deterministic, and it's ORT's optimised path for exactly these quantised convnets."*
