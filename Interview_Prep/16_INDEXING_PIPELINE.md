# 16 — The Indexing Pipeline

> Indexing is a completely different engineering problem from search. Search is latency-bound and interactive;
> indexing is throughput-bound, long-running, thermally constrained, and must survive being killed.

## The two phases

### Phase A — discovery (`startStoring`)

```
scanPhone() → 4 MediaStore cursors → classify → FileMeta
   → accumulate ALL into List<FileMeta>        ⚠️ see below
   → ONE JNI call with the whole array
   → insert_files(): BEGIN; INSERT OR IGNORE × N; COMMIT
```

- **`INSERT OR IGNORE`** + `abs_path TEXT NOT NULL UNIQUE` makes rescanning **idempotent**. Re-running the scan
  after adding 50 photos inserts 50 rows and silently ignores the rest. ✅ Simple and correct.
- **One transaction for the whole batch.** Without it, SQLite would fsync per row; with it, one commit. This is the
  difference between minutes and seconds for 100k inserts. ✅
- ⚠️ **Deleted files are never removed.** There is no reconciliation pass; a photo deleted from the gallery keeps its
  `files` row, its CLIP vector, its face detections and its graph edges forever, and will still be returned by search
  as a broken image. A `DELETE FROM files WHERE abs_path NOT IN (current scan)` pass is missing.
- ⚠️ **Modified files are never re-indexed.** `mtime_unix` is stored but never compared, and `INSERT OR IGNORE`
  discards the new row. An edited photo keeps its stale embedding.
- ⚠️ The 200-file batching in `scanPhone` is defeated by the caller — see [06](./06_JNI_BRIDGE.md).

### Phase B — embedding (`generateEmbeddingsNative`)

```cpp
while (!g_pause_embedding.load()) {
    std::vector<file_meta> images = get_unindexed_images(db_path, 10);   // own read-only connection
    if (images.empty()) break;
    for (const auto& img_meta : images) {
        try {
            CLIP_instance clip_img(path);                       // cv::imread  ← DECODE #1
            auto embedding = g_clip_session->get_embedding(clip_img);
            if (save_clip_embedding(db, img_meta.id, embedding)) {   // sets status='indexed'
                if (run_face_pipeline(db, path, img_meta.id, *g_face_session)) {  // ← DECODE #2
                    cluster_faces_for_file(db, img_meta.id);
                    update_graph_for_file(db, img_meta.id);
                }
                g_index_processed_count++;
            } else {
                update_file_status(db, img_meta.id, "failed");
            }
        } catch (const std::exception& e) {
            update_file_status(db, img_meta.id, "failed");
        }
    }
}
if (!paused) { recluster_pending_faces(db); merge_similar_entities(db); }
```

---

## Design decisions worth defending

### ✅ Batch of 10, driven by `status = 'pending'`

The loop is **restartable by construction**: progress is the `status` column, not an in-memory cursor. Kill the app
mid-index and nothing is redone — the next run picks up exactly where it left off. That's a genuinely good design
and costs nothing.

**Why 10 and not 1 or 1000?** Honest answer: *"it bounds the work between DB round-trips without holding a large
result set. Each iteration opens a fresh read-only connection, so a very small batch would mean many connection
opens, and a very large one would mean a long window where the `pending` snapshot goes stale. 10 is a reasonable
middle, but I never tuned it — and since the read is trivially cheap compared to ~500 ms of inference per image,
the batch size is not actually load-bearing. **The batching that would matter is batching the ONNX inference,
which I don't do at all.**"*

### ✅ CLIP before faces, with a status ordering that avoids lost work

`save_clip_embedding` sets `status='indexed'` *before* `run_face_pipeline` runs. So if face processing crashes, the
CLIP work is already durable and won't be redone. The cost is that the file is marked indexed even though its faces
weren't processed — and there's no `face_status` column to catch that. `resetFaceData` exists as the blunt recovery.

### 🔴 Every image is read from disk and JPEG-decoded twice

```cpp
CLIP_instance clip_img(path);      // → cv::imread(path)                      ← full decode
run_face_pipeline(db, path, ...);  // → ifstream read + cv::imdecode(buf)     ← full decode again
```

For a 12 MP JPEG that is two full reads and two full baseline-JPEG decodes per photo. **[ESTIMATED]** JPEG decode of
a 12 MP image on a mobile CPU is on the order of 50–150 ms, so this wastes roughly **50–150 ms per photo** — plausibly
**10–25% of total indexing time**, and it doubles peak memory because a full-resolution `cv::Mat` is alive in both
places.

**Fix:** decode once into a `cv::Mat`, pass it to both. `CLIP_instance` already has a `cv::Mat&` constructor, and
`run_face_pipeline` already takes a decoded `cv::Mat` internally. This is perhaps 15 lines and it is the single
highest-value indexing optimisation in the repo.

### 🔴 Batch size 1 for all inference

```cpp
std::vector<int64_t> input_shape = {1, 3, 224, 224};   // always 1
```

But the exported CLIP graph declares `dynamic_axes={"IMAGE": {0: "batch_size"}}` — **it already supports batching**
and the engine never uses it. Batching 4–8 images per `Run()` amortises kernel launch, improves cache and GEMM
efficiency, and lets ORT parallelise across the batch dimension.

**[ESTIMATED]** a 1.5–2.5× throughput improvement on the CLIP stage, which is the most expensive stage. `Reads/03`
identifies this too ("Batch size 4-8 could improve throughput 2-3x") — so it's a known, unimplemented idea.

Caveat to state: batching raises peak memory (`B × 3 × 224 × 224 × 2 B` for the FP16 input alone) and makes error
attribution coarser — one corrupt image fails the whole batch unless you pre-validate.

### 🟠 Everything is single-threaded

One indexing thread. Per image: decode → CLIP → decode → detect → align+embed per face → cluster → graph.
None of it overlaps.

The natural design is a **pipeline with bounded queues**:

```mermaid
flowchart LR
    A["Reader thread<br/>decode JPEG"] -->|bounded queue| B["Inference thread<br/>CLIP + SCRFD + ArcFace"]
    B -->|bounded queue| C["DB thread<br/>inserts, clustering, graph"]
```

Decode is memory/IO-bound, inference is compute-bound, DB is IO-bound — they overlap well. But note the real
constraint: **ORT already uses all cores for intra-op parallelism by default**, so adding threads doesn't multiply
throughput; it fills the gaps while ORT is idle (during decode and DB writes). **[ESTIMATED]** maybe 1.3–1.6×, not
3×. Being precise about that distinction — *"the CPU is already saturated during inference, so pipelining recovers
the non-inference time, not a linear speedup"* — is exactly the kind of answer that distinguishes a systems engineer
from someone reciting "use more threads".

Also: SQLite writes must stay single-writer, and there's no `busy_timeout`, so the DB stage must remain one thread.

### 🟠 The face pipeline is per-face, not batched

```cpp
for (const auto& face : faces) {
    cv::Mat aligned = align_face(image, face);      // 112×112
    // ... build a 1×3×112×112 tensor, Run(), extract 512 floats ...
}
```

`w600k_r50.onnx` declares input `[None, 3, 112, 112]` — **the batch dimension is already dynamic**. A group photo
with 8 faces does 8 separate `Run()` calls that could be 1. Same fix, same argument.

Also inside that loop: `GetInputNameAllocated` / `GetOutputNameAllocated` are called **per face**, allocating a
string each time. Cache them at load.

### 🟠 Pause is cooperative and coarse

`g_pause_embedding` is checked **only at the top of the `while` loop**, i.e. between batches of 10 — so pressing
pause can take up to 10 images (**[ESTIMATED]** ~5 seconds) to take effect. Moving the check inside the inner
`for` loop would make it ~1 image. Also, pausing **skips the reclustering and merge passes entirely**, so a
paused-then-resumed index ends in a different state than an uninterrupted one — because the resume path re-enters
the same function and will run them when it finally drains.

---

## Throughput

### Per-image cost breakdown

> ⚠️ **No benchmark harness exists in this repository.** `Reads/03` contains a latency table, but there is no script,
> no recorded output, and `vanta_logs.txt` contains only system logcat noise with zero engine lines. Everything in
> this table is therefore **[ESTIMATED]** or **[UNKNOWN]**, and the "documented" column is the repo's unverified
> claim. **Do not quote these as measurements.**

| Stage | `Reads/03` claims | Label | Reasoning |
|---|---|---|---|
| Image read + decode (×2) | ~5–10 ms | **[ESTIMATED] 100–300 ms** | The doc appears to count one decode of an already-small image. A 12 MP JPEG decode is 50–150 ms, and Vanta does it twice |
| CLIP ViT-B/32 image | ~150–200 ms | **[ESTIMATED] plausible** | ~8.7 GFLOPs at FP16 on a mobile CPU |
| FP32→FP16 conversion | not listed | **[ESTIMATED] 5–15 ms** | 150,528 scalar conversions |
| SCRFD 640×640 (INT8) | ~40–60 ms | **[ESTIMATED] plausible** | ~10 GFLOPs but INT8 and heavily optimised |
| Tensor packing loop | not listed | **[ESTIMATED] 10–30 ms** | 1.2 M `at<Vec3f>()` calls with a cache-hostile access pattern |
| ArcFace per face (INT8) | ~50–100 ms | **[ESTIMATED] plausible** | ResNet50 on 112×112 |
| Crop write (`imwrite` 320×320) | not listed | **[ESTIMATED] 5–20 ms/face** | JPEG encode + disk write |
| Clustering + graph | ~10–20 ms | **[ESTIMATED] plausible** | a k=1 KNN over a small table + a handful of inserts |
| **Total per image (1 face)** | ~400–650 ms | **[ESTIMATED] 400–800 ms** | |
| **Throughput** | ~2–3 img/s | **[ESTIMATED] 1.5–2.5 img/s** | single-threaded |

### Projected wall-clock

At **[ESTIMATED]** 2 images/sec, single-threaded, no throttling:

| Library | Time | Reality |
|---|---|---|
| 1,000 | ~8 min | Acceptable |
| 10,000 | ~1.4 h | Tolerable overnight |
| 100,000 | **~14 h** | 🔴 Needs multiple sessions; thermal throttling will make it worse |
| 500,000 | **~70 h** | ❌ Not viable |

**With thermal throttling** the sustained rate is materially lower than the burst rate — a phone will not hold peak
clocks for 14 hours. **[UNKNOWN]** by how much; this needs measurement.

**With the three fixes above** (single decode, batch 4–8, pipelined threads):
**[ESTIMATED] 3–5×** → 100k photos in **~3–5 hours**. That's the difference between "unusable" and "leave it
plugged in overnight".

---

## How to actually measure this (and what to say when asked)

> "I don't have these numbers, and I'd rather say that than make them up. Here's exactly how I'd get them."

**1. Instrument the C++ stages.** The engine already uses `std::chrono` in `search_images`; do the same per stage in
the indexing loop and accumulate into an atomic histogram exposed through `getIndexProgressNative`.

**2. Use Android's own tooling.**

| Tool | What it gives |
|---|---|
| **Perfetto / `systrace`** | Whole-system timeline: per-thread CPU, frequency, thermal state, IO. Add `ATrace_beginSection`/`ATrace_endSection` around each stage and you get a flame chart of the real pipeline |
| **`simpleperf`** | Sampling profiler with native symbols. `simpleperf record -g` → which C++ functions actually dominate. This settles the "is the packing loop hot?" question in minutes |
| **Android Studio Profiler** | Memory (native heap via `malloc` hooks), CPU, energy |
| **`dumpsys batterystats`** + **Battery Historian** | Attributed energy per UID over a session |
| **`adb shell dumpsys thermalservice`** | Throttling status over time |

**3. Build a fixed benchmark corpus** — e.g. 1,000 public-domain photos with a known face-count distribution,
committed as a script (not the images). Run indexing on a specific device model, report median and p95 per stage,
and re-run it on every change. That's the missing piece: **there is no regression baseline**, so any optimisation
claim is currently unfalsifiable.

---

## A battery-conscious indexing design

Vanta currently indexes on demand, at full speed, regardless of battery, temperature or charging state. The design
to propose:

```
Policy:
  - Run only when: charging OR battery > 50%
  - Pause when:    thermal status >= THROTTLING (PowerManager.getCurrentThermalStatus())
                   OR battery < 15%
                   OR the user is actively interacting (foreground, scrolling)
  - Prefer:        screen off + charging  → full speed, all cores
  - Otherwise:     throttled mode — cap ORT intra-op threads to 2, sleep 50 ms between images
  - Chunk work:    WorkManager with setRequiresCharging(true), setRequiresBatteryNotLow(true),
                   setRequiresDeviceIdle(true) for the bulk pass
  - Expose:        a user-visible "Index now (uses battery)" vs "Index while charging" choice
```

**The Android APIs to name:** `PowerManager.getCurrentThermalStatus()` (API 29+) and
`addThermalStatusListener`, `BatteryManager.EXTRA_LEVEL` / `ACTION_BATTERY_CHANGED`,
`WorkManager` + `Constraints.Builder()`, and `JobScheduler`'s idle/charging constraints underneath.

**On DVFS and why "fast is efficient" is not simply true:** phones scale voltage and frequency together, and dynamic
power goes roughly as $P \propto C V^2 f$ with $V$ rising with $f$ — so energy per unit of work is **worse** at
peak clocks. Running the indexer at a lower, sustained clock can complete the same job using **less total energy**,
even though it takes longer in wall-clock terms. And once the SoC hits a thermal trip point it throttles anyway, so
burst-speed indexing largely converts battery into heat without buying proportional throughput.

> **The honest closing line:** *"I never measured Vanta's battery impact, and I'm not going to quote a percentage I
> don't have. What I can tell you is the shape of the problem — it's a sustained, CPU-bound, multi-hour job, which
> is the worst possible workload for a phone — and the concrete mitigations, which are thermal- and charge-aware
> scheduling plus making the per-image work 3–5× cheaper so there's simply less of it to do."*
