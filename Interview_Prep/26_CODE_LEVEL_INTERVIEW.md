# 26 — Code-Level Interview

> "Open your repo. Explain this line." Every snippet is real, with its file and function.

---

## `vanta.cpp`

### Q. Why are `GetFieldID` calls outside the loop but `GetObjectField` inside?

```cpp
jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
jfieldID absPathField = env->GetFieldID(fileMetaClass, "absPath", "Ljava/lang/String;");
// ...11 more, all hoisted
for (jsize i = 0; i < count; ++i) {
    jobject fileObj = env->GetObjectArrayElement(files, i);
    jstring jAbsPath = (jstring)env->GetObjectField(fileObj, absPathField);
```

> "A `jfieldID` is a stable handle to a field *of the class*, so it's the same for every instance — computing it
> once is correct and necessary. `GetFieldID` resolves a UTF-8 name and type descriptor against the class's field
> table, so doing it per row would dominate the loop. `GetObjectField` reads the value from a specific instance, so
> it has to be inside.
>
> The one caveat is that a `jfieldID` is only valid while the class stays loaded — if you cached it across calls in
> a static you'd need a global ref to the class. Here it's per-call, so it's fine."

### Q. Why `DeleteLocalRef` on every object?

> "Because the local reference table is small — the JNI spec guarantees only 16 slots and ART typically gives ~512 —
> and every `jobject` or `jstring` JNI hands back occupies one until the native method returns. With 100,000 files ×
> ~5 objects each, omitting these would abort the VM with
> `JNI ERROR (app bug): local reference table overflow`. It's the single most common JNI bug and this loop avoids it."

### Q. Why is `SQLITE_TRANSIENT` used here but `SQLITE_STATIC` there?

```cpp
// clip_db.cpp — TRANSIENT
sqlite3_bind_blob(stmt, 2, embedding.data(), embedding.size()*sizeof(float), SQLITE_TRANSIENT);
// face_DB.cpp — STATIC
sqlite3_bind_blob(stmt, 2, embedding.data(), embedding.size()*sizeof(float), SQLITE_STATIC);
```

> "`SQLITE_STATIC` promises SQLite that the buffer will outlive the statement, so it stores the pointer and copies
> nothing. `SQLITE_TRANSIENT` tells SQLite to make its own copy immediately.
>
> Both uses here are safe — in each case the vector is a live function parameter that outlives the `step`. The place
> the distinction genuinely matters is `cluster_faces_for_file`, where `new_centroid` is a **local** computed inside
> the loop, and that one correctly uses `SQLITE_TRANSIENT`. If it used `STATIC`, SQLite would hold a pointer to a
> vector destroyed at the end of the iteration. Getting that mix right is the whole game with `bind_blob`."

### Q. Why do `IndexingService` and `VantaEngineModule` both have a `setModelsDirNative`?

> "Because JNI resolves by **class-qualified** mangled name. `IndexingService` declares its own `external fun`, so
> the runtime looks for `Java_expo_modules_vantaengine_IndexingService_setModelsDirNative`. I define thin forwarding
> shims for the Service's three natives.
>
> The shim passes `nullptr` for the `jobject this`, which is safe **only** because none of these functions
> dereference it. If a future version used `this`, it would crash. `RegisterNatives` would let one implementation
> serve both class names properly."

### Q. Why are `g_clip_session` and friends never deleted?

> "Deliberate process-lifetime leak. They're expensive to construct — an `Ort::Session` runs the full graph
> optimisation pass — so I create them lazily on first use and keep them. The cost I'd flag is that
> CLIP-image, SCRFD and ArcFace stay resident after indexing finishes, which is hundreds of megabytes doing nothing.
> Both model classes implement `unload()` and **nothing ever calls it**. The `IndexingService` stopping is the
> obvious place."

### Q. Point at a race in this file.

```cpp
{
    std::lock_guard<std::mutex> lock(g_session_mutex);
    if (!g_clip_session) g_clip_session = new CLIP_session();
    /* ...load CLIP... */
    if (!g_tokenizer) g_tokenizer = new CLIPTokenizer();
}                                     // ← lock released

if (!g_tokenizer->is_loaded()) {      // ← UNSYNCHRONISED
    g_tokenizer->load(vocab_path, merges_path);   // mutates unordered_maps
}
```

> "Right there. Session creation is inside the mutex; **the tokenizer load is outside it**. Every search from JS
> spawns a raw Kotlin `Thread` with no serialisation, so two searches issued before the first finishes loading will
> both enter that block and concurrently mutate `vocab_` and `bpe_ranks_`. That's undefined behaviour, and the
> realistic symptom is a crash on the first two rapid searches after a cold start. The fix is moving the load
> inside the existing lock scope."

---

## `query_engine.cpp`

### Q. Why does `resolve_span_to_entity_id` start `best_dist` at 3 but return on `<= 2`?

```cpp
int best_dist = 3;
// ... if (d < best_dist || (d == best_dist && c.sample_count > best_count)) { ... }
return best_dist <= 2 ? best_id : -1;
```

> "3 is a sentinel meaning 'worse than anything acceptable', so the first candidate at distance ≤ 2 always wins the
> `d < best_dist` comparison. The final `<= 2` is the actual accept threshold.
>
> The tie-break is the interesting part: when two entities are equally close, prefer the one with more samples — the
> person who appears more often is the more likely referent. That's a sensible prior.
>
> The bug is the threshold itself. Distance 2 on short strings is far too loose: **'dad' to 'mom' is distance 2, and
> 'mom' to 'tom' is distance 1.** So a user named Tom could be returned for 'mom'. It should scale with length —
> something like `max_dist = len <= 4 ? 1 : 2`."

### Q. Why does the restricted path use `WHERE rowid = ?` instead of a KNN?

```cpp
const char* emb_sql = "SELECT file_id, embedding FROM clip_vec WHERE rowid = ?";
```

> "Because the graph already chose the candidates. This is a **point lookup**, not a search — I fetch that specific
> file's stored vector and score it in C++ with a dot product. Running a KNN would be asking sqlite-vec to search
> the whole table when I already know exactly which 40 rows I want.
>
> It's also the cheaper half of the ordering argument: 40 point lookups plus 40 dot products beats scanning 100,000
> vectors by three orders of magnitude."

### Q. Why is `distance` computed two different ways?

```cpp
res.distance = 1.0f - sc.score;   // restricted path: cosine distance
res.distance = h.distance;        // global KNN: sqlite-vec L2 distance
```

> "That's an inconsistency, and it's a real one. In the restricted path I compute cosine myself and report
> `1 − cosine`. In the global path I pass through sqlite-vec's distance, which is **L2** because I didn't specify
> `distance_metric=cosine` on the `vec0` table.
>
> Those are different scales — related by $L2^2 = 2 - 2\cos$, but not equal. So the same JSON field means two
> different things depending on which branch ran, and the UI can't compare them. I'd normalise both to cosine
> distance."

### Q. What's wrong with this "covered" test?

```cpp
for (const auto& st : resolved_span_texts)
    if (st.find(lw) != std::string::npos || lw.find(st) != std::string::npos) { covered = true; break; }
```

> "It's **substring containment in both directions** when it should be exact token equality. If an entity is named
> 'Ann', then the word 'banner' contains 'ann' and gets dropped from the CLIP caption. It's a small bug but it
> silently removes query terms, which is the same failure family as the Intent Builder problem."

### Q. Explain the smallest-set optimisation.

```cpp
size_t smallest_idx = 0;
for (size_t i = 1; i < sets.size(); ++i)
    if (sets[i].size() < sets[smallest_idx].size()) smallest_idx = i;
for (int64_t fid : sets[smallest_idx]) { /* probe every other set */ }
```

> "Standard k-way set intersection. The result can't be larger than the smallest input set, so you iterate that one
> and probe the rest — O(min |S| × k) hash lookups instead of O(Σ|S|). If one person appears in 20 photos and
> another in 5,000, you iterate 20 and do 20 probes rather than touching 5,000 elements."

### Q. What does this fallback silently do to correctness?

```cpp
if (candidate_file_ids.empty()) {
    LOGI("Intersect empty. Falling back to UNION to provide some results.");
    require_all_entities = false;
    /* union of all membership sets */
}
```

> "It **relaxes the constraint the user asked for.** 'Me and mom' becomes 'me *or* mom', and because
> `require_all_entities` is now false, `verify_entities_in_file` only requires **one** match instead of all. So
> results can contain just one of the two people. My own documentation claims the traversal 'guarantees that we only
> look at photos where all requested people are physically present together' — and this path breaks that guarantee
> without telling anyone. I'd either drop it or mark those results as partial in the response."

---

## `face_DB.cpp`

### Q. Walk me through this centroid update.

```cpp
for (int i = 0; i < 512; ++i) {
    new_centroid[i] = old_centroid[i]*0.9f + face.embedding[i]*0.1f;
    norm += new_centroid[i]*new_centroid[i];
}
norm = std::sqrt(norm);
if (norm > 0) for (int i = 0; i < 512; ++i) new_centroid[i] /= norm;
```

> "It's an exponential moving average with α = 0.1, followed by re-normalisation to the unit sphere — which is
> necessary because a convex combination of two unit vectors isn't unit length, and all my thresholds assume unit
> vectors.
>
> But I'd flag two things. First, **this isn't a centroid**, despite the table name and my own docs, which describe
> a running mean and call EMA the 'alternative'. Second, the consequences are real: it's **order-dependent**, so
> re-indexing in a different order produces different identities; it's recency-biased, since after ~30 assignments
> the earliest faces contribute under 4%; and `sample_count` never enters it, so a 500-photo identity adapts as fast
> as a 3-photo one."

### Q. Why is this `INSERT` missing a column?

```cpp
INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at) VALUES (?, ?, ?)
```

> "Because `score` is omitted, so it takes the column default of **1.0** — on every row, everywhere in the codebase.
> I verified this by running the real schema.
>
> That breaks a feature that's otherwise fully written: `get_combined_membership_scores` computes the **min** score
> across the required entities, on the correct reasoning that 'a photo of me and mom is only as good as its worst
> face match'. But every value is 1.0, so everything ties and results come back in `unordered_map` iteration order.
> `get_entity_files`'s `ORDER BY em.score DESC` is likewise a no-op.
>
> The fix is three lines — the cosine is already in hand from the `person_centroids MATCH` a few lines above; I just
> drop it instead of carrying it into the insert."

### Q. What happens here, exactly?

```cpp
int64_t final_entity_id = -1;   // -1 means ignore permanently
// ...
sqlite3_bind_int64(update_det_stmt, 1, final_entity_id);
sqlite3_step(update_det_stmt);   // return value discarded
```

> "It fails, silently. `face_detections.entity_id` is `INTEGER REFERENCES entities(entity_id)` and
> `initialize_database` sets `PRAGMA foreign_keys = ON`. There's no entity with id −1, so the UPDATE is rejected
> with a foreign-key constraint failure — I confirmed that by running the real schema.
>
> So the tombstone never persists. Those detections stay `entity_id IS NULL` forever and get re-fetched and
> re-failed on **every** reclustering pass, indefinitely. And it's invisible because `sqlite3_step`'s return code is
> discarded, so the log line reports success.
>
> Sentinel-in-a-foreign-key was the wrong design. It should be a separate `state` column or a nullable
> `rejected_at`. But the deeper bug is the unchecked return code — that's the habit that hid it."

### Q. Why is `merge_similar_entities` O(n²), and why does it get slower than that?

```cpp
for (size_t i = 0; i < all_entities.size(); ++i) {
  for (size_t j = i+1; j < all_entities.size(); ++j) {
     /* 512-d L2 */
     if (dist <= MERGE_THRESHOLD) { /* 7 × sqlite3_prepare_v2 + step + finalize */ }
```

> "All-pairs comparison — at 5,000 entities that's 12.5 million 512-dimensional L2 computations. Worse, every merge
> **re-prepares seven SQL statements inside the inner loop** instead of preparing once and resetting, so SQLite
> re-parses and re-plans each one every time.
>
> The right fix isn't micro-optimisation, it's using the index I already have: query `person_centroids` with a KNN
> to get candidate pairs, which is O(n·k) instead of O(n²).
>
> And there's a correctness issue in the same loop: `all_entities[i].embedding` is updated in place and the `j` scan
> continues, so merges **chain** — A into B, then the blended A′ into C. One greedy pass with a loose threshold can
> cascade several distinct people into one entity."

### Q. Where does this function lose data?

> "At the end. It moves `face_detections` and `entity_memberships` to the keeper, then does
> `DELETE FROM entities WHERE entity_id = merged_id`. It never touches `entity_relations` or
> `entity_relation_files`, which declare `ON DELETE CASCADE` — so the delete **cascades away every co-occurrence
> edge** involving the merged entity instead of transferring it.
>
> I verified this by running the real schema: edges before the delete, empty after. The visible effect is that the
> O(1) two-person lookup stops working for merged identities and falls back to set intersection — still correct,
> which is exactly why it went unnoticed — and `co_occurrence_count` history is permanently lost."

---

## `Seg_model.cpp`

### Q. Explain these magic numbers.

```cpp
if (num_anchors == 12800) stride = 8;
else if (num_anchors == 3200) stride = 16;
else if (num_anchors == 800) stride = 32;
```

> "They're the SCRFD output geometry at a 640×640 input. Feature map sizes are 640/8 = 80, 640/16 = 40,
> 640/32 = 20, and SCRFD has **2 anchors per spatial location**. So 80·80·2 = 12800, 40·40·2 = 3200,
> 20·20·2 = 800. I identify which output tensor is which stride by its anchor count, and the last dimension tells me
> the type: 1 for scores, 4 for box distances, 10 for five keypoint offsets.
>
> The fragility is that these are **hardcoded to 640**. The ONNX input is dynamic (`[1,3,?,?]`), so feeding any
> other size makes every `stride` stay 0, every output gets skipped, and the function returns **zero detections with
> no error**."

### Q. Why `cx - dx_min` and `cx + dx_max`?

```cpp
float x1 = cx - bboxes[i*4+0]*stride;
float x2 = cx + bboxes[i*4+2]*stride;
```

> "Because SCRFD is anchor-free and regresses **distances**, FCOS-style: for each anchor centre it predicts how far
> the left, top, right and bottom edges are, in stride units. So you subtract for the left/top edges and add for
> right/bottom, scaling by the stride to get pixels. That's different from anchor-based detectors like YOLO, which
> predict deltas relative to a prior box's width and height."

### Q. What's missing from this preprocessing?

```cpp
padded.convertTo(float_img, CV_32FC3, 1.0f/128.0f, -127.5f/128.0f);
for (int c = 0; c < 3; ++c) /* ... */ float_img.at<cv::Vec3f>(h,w)[c];
```

> "The **BGR→RGB swap**. The normalisation matches InsightFace exactly — `(x − 127.5)/128` — but their preprocessing
> is `cv::dnn::blobFromImage(..., swapRB=True)`, so SCRFD was trained on RGB. `cv::imdecode` gives me BGR and I
> index the channels straight through.
>
> The function's own comment claims 'Reorder channels: BGR → RGB', and `grep cvtColor` on this file returns exactly
> one hit — at line 509, in the *recognition* path. So the two halves of my own face pipeline disagree about colour
> convention, which tells you it was an oversight rather than a decision.
>
> It degrades rather than destroys detection, because detection leans heavily on luminance structure. But it costs
> me exactly the marginal faces — small, dim, side-lit — and a missed detection means that photo can never be found
> by person. The right fix replaces this entire loop with `cv::dnn::blobFromImage(..., swapRB=true)`, which also
> removes **1.2 million `at<Vec3f>()` calls per image** with a cache-hostile stride."

### Q. Why `estimateAffinePartial2D` and not `estimateAffine2D`?

> "Partial fits a **4-DoF similarity** transform — rotation, uniform scale, translation. The full affine adds shear
> and independent x/y scaling.
>
> A similarity is correct here because shear and anisotropic scaling would distort **facial geometry**, and facial
> geometry *is* the identity signal ArcFace reads. With a full affine, the transform could 'correct' a person's
> actual proportions toward the template — literally erasing the information you're trying to measure. You want to
> remove pose-induced rotation and scale while preserving shape."

---

## `CLIP_image.cpp` / `CLIP_model.cpp`

### Q. What's wrong with this resize?

```cpp
cv::resize(image, image, cv::Size(224, 224));
```

> "It's an **anisotropic squash**. Reference CLIP resizes the shortest side to 224 with bicubic interpolation and
> then centre-crops. A 16:9 photo here gets stretched 1.78× horizontally — faces come out visibly wide. That's a
> train/inference preprocessing mismatch affecting **100% of my index**, silently.
>
> I'd note the fix isn't free either: centre-cropping a 16:9 frame throws away 44% of it, and subjects in personal
> photos aren't always centred. I'd want to A/B reference-crop against pad-to-square-then-resize — but only after
> building an eval harness, because right now I can't tell which is better."

### Q. Spot the asymmetry between these two functions.

```cpp
// get_embedding (image):
float norm = 0.0f; for (float x : embedding) norm += x*x; norm = std::sqrt(norm);
for (float& x : embedding) x /= norm;                    // ← no guard

// get_text_embedding:
norm = std::sqrt(norm);
if (norm > 0.0f) { for (float& x : embedding) x /= norm; }   // ← guarded
```

> "The image path divides by `norm` unconditionally. If a degenerate input produced an all-zero embedding, that's
> division by zero → 512 NaNs, written straight into `clip_vec`, where they'd poison every subsequent KNN — NaN
> comparisons are false, so the distance ordering becomes undefined. Very low probability, trivially fixed, and the
> text path already shows I knew to guard it."

### Q. What does this loop cost?

```cpp
for (size_t i = 0; i < fp16_data.size(); ++i) fp16_data[i] = Ort::Float16_t(src[i]);
```

> "150,528 scalar FP32→FP16 conversions per image — 1×3×224×224 — on the indexing critical path, **[ESTIMATED]**
> 5–15 ms each time. It exists because I converted the model to FP16 for **size**: ViT-B/32 is ~600 MB in FP32 and
> ~300 MB in FP16, and APK size was my binding constraint.
>
> The irony is worth stating: I'm paying a per-image conversion cost to use a format I adopted for disk savings,
> and I never benchmarked whether FP16 is actually faster than FP32 here. On a CPU without the ARMv8.2 FP16
> extension it could be slower. The clean fix is exporting a model with FP32 inputs and FP16 weights, so ORT does
> the cast internally where it can vectorise it."

---

## Schema & build

### Q. Which indexes does `init_face_schema` create?

> "**None.** It creates five tables — `entities`, `person_centroids`, `face_detections`, `face_vec`,
> `entity_memberships` — and not one `CREATE INDEX`. `DBschema.sql` declares `idx_face_det_file`,
> `idx_face_det_entity` and `idx_memberships_entity`, but that file is a design document that's never executed.
>
> `entity_memberships` survives by accident because its primary key `(entity_id, file_id)` has `entity_id` leading.
> `face_detections` doesn't: `verify_entities_in_file` runs `WHERE file_id = ? AND entity_id IN (…)` **once per
> search result** against a table with one row per face in the entire library. At 100k photos that's a 150,000-row
> full scan, thirty times per search. Two `CREATE INDEX` statements are probably my largest single search-latency
> win."

### Q. Why `#define SQLITE_CORE 1` before including `sqlite-vec.h`?

> "It makes sqlite-vec link **statically** against the amalgamation's symbols instead of going through the
> `sqlite3_api_routines` function-pointer table that runtime-loaded extensions use. Combined with
> `sqlite3_auto_extension((void(*)())sqlite3_vec_init)` before each `sqlite3_open`, it means `vec0` is registered
> automatically on every connection without ever touching `sqlite3_load_extension` — which matters, because that's
> the API Android doesn't expose."

### Q. Why is `GRAPH_SOURCES` set to an empty string?

```cmake
# NOTE: The Graph/ directory does not exist yet.
set(GRAPH_SOURCES "")
```

> "Honestly? It documents that the graph was an afterthought. I'd planned a `Include/Graph/` module and the graph
> code ended up in `DB/graph_db.cpp` instead, which is arguably the right place since it's all SQL. The empty
> variable and the commented-out glob are dead scaffolding I should remove."

### Q. What's in `thirdparty/sqlite_vec/` that you're not using?

> "`sqlite-vec-diskann.c`, `sqlite-vec-ivf.c`, `sqlite-vec-ivf-kmeans.c` and `sqlite-vec-rescore.c` — the ANN index
> implementations. **None of them is in the `add_library` list**; only `sqlite-vec.c` is compiled. So every KNN in
> Vanta is an exhaustive brute-force scan, and the approximate-nearest-neighbour code I'd need to fix that at scale
> is already sitting in my repository, uncompiled."
