# 01 — Vanta Reality Check

> **Read this before anything else.** This is the gap between what Vanta's README/`Reads/` claim and what the code does.
> An interviewer who opens your repo will find these. You need to find them first and have an answer ready.

Audited commit `fa53057`, branch `main`, 2026-09-21. Everything below was read out of source, ONNX graphs, or verified
by executing the actual SQL schema.

---

## A. The three findings I verified by execution

These are not inferences. I rebuilt Vanta's real schema in SQLite and ran the code's own SQL against it.

### A1. `entity_memberships.score` is always exactly `1.0` — the "min-score" ranking is a no-op

The schema declares `score REAL DEFAULT 1.0`. Every insert in the codebase omits the column:

```sql
-- cpp/DB/face_DB.cpp, cluster_faces_for_file() and recluster_pending_faces()
INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at) VALUES (?, ?, ?)
-- merge_similar_entities() likewise:
INSERT OR IGNORE INTO entity_memberships (entity_id, file_id, created_at)
  SELECT ?, file_id, created_at FROM entity_memberships WHERE entity_id = ?
```

`score` is **never written anywhere in the repository.** Execution result on the real schema:

```
entity_memberships -> [(1, 1, 1.0), (2, 1, 1.0), (2, 2, 1.0)]
```

**Consequences, all real:**

- `get_combined_membership_scores()` (`query_engine.cpp`) computes `min(score)` across the required entities. Every
  value is 1.0, so **every candidate file ties at 1.0**. The "people-only" ranking branch (Scenario B) returns files in
  `std::unordered_map` iteration order — i.e. effectively arbitrary.
- `get_entity_files()` does `ORDER BY em.score DESC` — also a no-op.
- `search_result.distance` for that branch is `1.0f - 1.0f = 0.0` for every row.

**The docs claim otherwise.** `Reads/GraphTraversalLogic.md` says score is "the facial detection confidence score
(`det_score` in `entity_memberships`)". `Reads/06-Graph-Database.md` says "Score computation: The average cosine
similarity between the face embedding in this file and the entity's centroid." Neither is implemented.

> **Interview answer:** "The membership score column exists and the ranking code reads it, but nothing ever writes it,
> so people-only queries are currently unordered. The fix is three lines — I already have the cosine distance from the
> `person_centroids` MATCH in `cluster_faces_for_file`; I need to carry `1 - distance²/2` into the membership insert
> instead of dropping it."

---

### A2. The "permanently ignore this face" path silently fails on a foreign-key constraint

`recluster_pending_faces()` uses `entity_id = -1` as a tombstone for faces that could not be matched even at the
relaxed threshold:

```cpp
int64_t final_entity_id = -1; // -1 means ignore permanently
...
sqlite3_bind_int64(update_det_stmt, 1, final_entity_id);
sqlite3_step(update_det_stmt);   // <-- return value never checked
```

But `face_detections.entity_id` is `INTEGER REFERENCES entities(entity_id) ON DELETE SET NULL`, and
`initialize_database()` sets `PRAGMA foreign_keys = ON`. There is no entity with id `-1`. Execution result:

```
UPDATE REJECTED -> FOREIGN KEY constraint failed
row entity_id is still: NULL
```

**Consequences:** the tombstone never persists. Those detections stay `entity_id IS NULL` forever, so
`recluster_pending_faces()` re-fetches, re-matches and re-fails on **every** indexing run and every
`resetFaceData`. The work is O(unmatched faces) repeated indefinitely, and the "second pass" log line reports success
because `sqlite3_step`'s return code is discarded.

> **Interview answer:** "Sentinel-in-a-foreign-key was the wrong design. The right fix is a nullable
> `rejected_at INTEGER` column or a separate `face_detections.state` enum — never a magic FK value. It's masked because
> I don't check `sqlite3_step`'s return code there, which is the deeper bug."

---

### A3. The entity merge pass destroys the co-occurrence edges it should be transferring

`merge_similar_entities()` moves `face_detections` and `entity_memberships` from the merged entity to the keeper, then
deletes the merged entity row. It never touches `entity_relations` / `entity_relation_files`. Those tables declare
`FOREIGN KEY (entity_a) REFERENCES entities(entity_id) ON DELETE CASCADE`, and FK enforcement is on. Execution result:

```
edges BEFORE deleting entity 2: entity_relations=[(1, 2, 1, 0, 0)]  entity_relation_files=[(1, 2, 1)]
edges AFTER  deleting entity 2: entity_relations=[]                 entity_relation_files=[]
```

**Consequences:** after the merge pass runs (end of every full index), the two-entity fast path
`SELECT file_id FROM entity_relation_files WHERE entity_a = ? AND entity_b = ?` returns nothing for any pair touching
a merged identity. The query engine degrades to the membership-intersection fallback — which still works, so the bug is
invisible in normal use. It quietly converts an O(1) indexed edge lookup into two full membership scans plus a
set intersection, and it permanently loses `co_occurrence_count` history.

> **Interview answer:** "The merge is not edge-aware. Before deleting the merged entity I need to re-point its edges at
> the keeper with an `INSERT ... ON CONFLICT DO UPDATE SET co_occurrence_count = co_occurrence_count + excluded....`,
> collapse any self-edge, then delete. The cascade hid it because the fallback path masks the symptom."

---

## B. Claim-by-claim audit table

Classification key: **A** = fully implemented · **B** = partially implemented · **C** = differs from documentation ·
**D** = planned/not built · **E** = cannot be verified from repo.

### B1. Models and ML

| Claim (source) | Actual implementation | File / function | Evidence | Class | Confidence | Interview risk |
|---|---|---|---|---|---|---|
| "DistilBERT NER (custom-trained)" — `README.md`, `Reads/09` | **BERT-tiny**: `BertForTokenClassification`, **2** encoder layers, hidden **128**, intermediate 512, 4,370,424 params, vocab 30,522, opset 18, FP32 | `.model/ner_model.onnx` | ONNX initializer dump: `bert.encoder.layer.{0,1}` only; `word_embeddings [30522,128]`; `classifier.bias [7]` | **C** | **Certain** | 🔴 **Critical** — DistilBERT is 6×768/66M. A 15× overstatement an interviewer can check in 30s |
| NER model "~66 MB" — `Reads/09` | **16.94 MB** | `.model/ner_model.onnx` | `ls -l` | **C** | Certain | 🔴 High |
| "Face Detection … (RetinaFace)" — `Reads/09` | **SCRFD-10GF** (`det_10g`). The C++ decoder is SCRFD-shaped: strides 8/16/32, **2 anchors per location**, anchor counts 12800/3200/800, 9 outputs `{1,4,10}` | `Seg_model.cpp::get_faces` | ONNX outputs `[12800,1] [3200,1] [800,1] [12800,4]… [800,10]`; `buffalo_l` ships SCRFD | **C** | **Certain** | 🟠 Medium — easy to be caught confusing detector families |
| "Current models use **FP16**" — `Reads/09` | **Mixed.** CLIP = FP16. Face models = **INT8 dynamic** | `produce_model/quantize.py` vs `download_models.py` | CLIP via `float16.convert_float_to_float16`. `det_10g`: 48× `DynamicQuantizeLinear`, 58× `ConvInteger`. `w600k_r50`: 53× `ConvInteger`, 1× `MatMulInteger` | **C** | **Certain** | 🔴 High — and the same doc lists INT8 as "Future" work that is already shipped |
| "`SetIntraOpNumThreads(4)`" — `Reads/09` | **Never called for CLIP or face.** Both use default-constructed `Ort::SessionOptions`. Only NER sets threads — to **1** | `CLIP_model.cpp`, `Seg_model.cpp`, `ner.cpp` | `opts.SetIntraOpNumThreads(1)` appears only in `ner.cpp::load` | **C** | Certain | 🟠 Medium |
| "Global environment (singleton) `static Ort::Env g_ort_env`" — `Reads/09` | **Three separate `Ort::Env` objects**, one per class | `CLIP_model.hpp::env`, `Seg_model.hpp::shared_env`, `ner.hpp::env_` | Member declarations | **C** | Certain | 🟠 Medium — ORT documents one `Env` per process |
| Tokenizer file `bpe_simple_vocab_16e6.txt` — `Reads/09` | Loads **`vocab.json` + `merges.txt`** | `vanta.cpp::searchImagesNative`, `CLIP_tokenizer.cpp::load` | `cfg.model_path("vocab.json")`, `("merges.txt")` | **C** | Certain | 🟢 Low |
| CLIP = ViT-B/32, 512-d, image+text | **Correct.** `clip.load("ViT-B/32")`, `model.visual` (includes `proj`) and `encode_text` (includes `text_projection`) both land in the shared 512-d space | `produce_model/get_onnx_CLIP.py` | Export script | **A** | Certain | 🟢 Low |
| ArcFace `buffalo_l` | **Correct and verified.** `w600k_r50.onnx` = 43,591,296 params, in `[None,3,112,112]`, out `[1,512]` | `Seg_model.cpp::load` | ONNX graph | **A** | Certain | 🟢 Low |
| `buffalo_sc` listed as a "variant" — `Reads/03`, `Reads/04` | `det_500m.onnx` (2.41 MB) and `w600k_mbf.onnx` (12.99 MB) are **committed but never loaded**. Paths are hardcoded to buffalo_l. Source comments still say "Buffalo_sc" | `Seg_model.cpp::load` | `model_path("det_10g.onnx")`, `("w600k_r50.onnx")` | **C** | Certain | 🟠 Medium — stale comments contradict the code in the same function |

### B2. Retrieval, graph, query

| Claim | Actual implementation | File / function | Evidence | Class | Confidence | Risk |
|---|---|---|---|---|---|---|
| "**Document Indexing** — PDFs, Word docs, and text files are chunked and embedded for full-text semantic search" — `README.md` | **Not implemented at all.** Documents are *discovered* and stored as `files` rows. The indexer selects `WHERE filetype = 'picture'`. `document_chunks` / `document_vec` exist only in `DBschema.sql`, which is never executed | `clip_db.cpp::get_unindexed_images`; `DBoperations.cpp::initialize_database` | Runtime DDL creates only `files`; no chunking/text-extraction code exists anywhere | **D** | **Certain** | 🔴 **Critical** — a flat false feature claim on the front page |
| Video indexing / "video keyframes" — `DBschema.sql` | Videos are scanned into `files` only. No decode, no keyframe, no embedding | same | `filetype='picture'` filter | **D** | Certain | 🔴 High |
| "SymSpell (Symmetric Delete)" spell correction — `Reads/05` | Algorithm **is** SymSpell-shaped (delete-neighborhood index + Damerau-Levenshtein verify). But the dictionary is **~40 hardcoded English words** plus entity names/relations from the DB. `SymSpellDictionary` is described in its own header as a "Mock definition" and is constructed empty | `typo_rectifyier.cpp`; `query_engine.cpp::init_query_engine` | `static_terms` vector, 40 entries | **B** | Certain | 🟠 Medium — say "SymSpell algorithm over a tiny domain dictionary", never "SymSpell" unqualified |
| Intent Builder mapping table — `Reads/05` | **Docs are wrong.** Doc says `beach → "beach scene"`, `temple → "temple architecture"`, `office → "office workspace"`. Code says `beach → "at a beach"`, `temple → "near a temple"`, `office → "in an office"` | `IntentBuilder.cpp` ctor | `placesMap` literal | **C** | Certain | 🟠 Medium |
| "If no mapping exists, the raw text is used" — `Reads/05` | True only when the intent output is **entirely** empty. If *any* word matches, unmatched words are **silently dropped**. `"my dog at the beach"` → `"at a beach"`; `dog` is lost | `IntentBuilder::buildIntent` + `query_engine.cpp` step 4 | `buildIntent` only ever emits `eventIntent`/`peopleIntent`/`placeIntent` | **C** | **Certain** | 🔴 **Critical** — this is a genuine retrieval-destroying bug, see §C1 |
| "Files are collected in batches of 200 and passed to the C++ layer via JNI" — `Reads/02` | `scanPhone` does batch at 200, but the caller accumulates **every** batch into one list and makes **one** JNI call with the whole array | `VantaEngineModule.kt::startStoring` | `scanPhone(context) { batch -> allFiles.addAll(batch) }` then `startStoring(dbPath, allFiles.toTypedArray())` | **C** | Certain | 🟠 Medium — but see §C2, the JNI story is actually *good* |
| "guarantees that we only look at photos where all requested people are physically present" — `Reads/GraphTraversalLogic` | There is an undocumented **UNION fallback**: if the intersection is empty, `require_all_entities` flips to `false` and the union is returned, with `verify_entities_in_file(require_all=false)` requiring only **one** match | `query_engine.cpp` step 5 | `LOGI("Intersect empty. Falling back to UNION…")` | **C** | Certain | 🟠 Medium — a correctness guarantee silently relaxed |
| 3+ entity intersection with smallest-set optimization | **Correct and implemented well** | `query_engine.cpp` step 5 | picks `smallest_idx` then probes | **A** | Certain | 🟢 Low |
| Burst-shot dedup (30 s window) | Implemented, but **only in the entity-restricted branch**. Global KNN results are not deduped. Also O(n²) over accepted results | `query_engine.cpp` step 6a | `is_burst` block absent from 6b | **B** | Certain | 🟢 Low |
| Face verification before returning results | Implemented, **only in 6a** (same asymmetry) | `verify_entities_in_file` | — | **B** | Certain | 🟢 Low |
| `entity_relations.co_occurrence_count` "for future weighting" | Maintained correctly by `update_graph_for_file`, but **never read by the ranker**. Only surfaced in the People UI | `graph_db.cpp` | `get_neighbors` is the sole reader | **B** | Certain | 🟢 Low |

### B3. Platform, build, process

| Claim | Actual | Evidence | Class | Risk |
|---|---|---|---|---|
| "Cross-platform" / iOS support (`expo-module.config.json` lists `apple`) | **iOS is a 15-line stub.** `VantaEngineModule.swift` exposes exactly two functions: `helloFromKotlin` → `"hi from swift"` and `helloFromCpp` → `"hi from swift (C++ not implemented on iOS yet)"`. No engine, no models, no DB | `ios/VantaEngineModule.swift` | **D** | 🔴 **Critical** if you ever say "cross-platform" |
| Android device coverage | **arm64-v8a only.** `abiFilters 'arm64-v8a'`, and the bundled `libonnxruntime.so` is aarch64-only | `android/build.gradle`; `file(1)` on the .so | **A** (deliberate) | 🟢 Low — defensible, just say it |
| CI runs on every push | The workflow lives at `vanta_app/.github/workflows/ci.yml`. **GitHub Actions only reads `.github/workflows/` at the repository root**, which does not exist here. So this workflow has never run | `ls .github` → not found | **D** | 🟠 Medium |
| CI "Build vanta-bridge" job | Even if relocated it would fail: it runs `cd android && ./gradlew …`, but `vanta_app/android` is **gitignored** (Expo prebuild output) and the workflow never runs `expo prebuild` | `.gitignore` line `vanta_app/android` | **D** | 🟠 Medium |
| `helloFromKotlin()` / `helloFromCpp()` exported from the public JS API | **Not implemented in the Kotlin module.** Declared in `VantaEngineModule.ts`, exported from `index.ts`, implemented in the iOS stub and the web stub — but `grep helloFrom android/` returns nothing. Calling either on Android throws | `modules/vanta-bridge/index.ts` vs `VantaEngineModule.kt` | **C** | 🟢 Low but embarrassing |
| `DBschema.sql` is "the" schema | It is a **design document that is never executed**. The live schema is assembled at runtime from `DBoperations.cpp` + `face_DB.cpp` + `graph_db.cpp`. `DBschema.sql` is missing `display_name`, `aligned_crop_path`, `relation`/`age`/`location`, `entity_relations`, `entity_relation_files`, and adds `document_chunks`/`document_vec` that do not exist | compare files | **C** | 🟠 Medium |
| README repo structure: root `cpp/`, `test_intent_builder` binary | Neither exists | `find` | **C** | 🟢 Low |
| `files.retry_count` | Column exists, is inserted, is read back — but is **never incremented**. Nothing retries a `failed` file | grep `retry_count` | **D** | 🟢 Low |
| `files.display_name` | `FileMeta.displayName` is populated in Kotlin but the JNI reader **never reads that field**, so every row stores `''`. Search results always return `display_name: ""` | `vanta.cpp::startStoring` field list | **C** | 🟠 Medium — a visible dead field in the API response |
| "~280 ms average search latency … peaks ~430-450 ms" — `README.md` | **Not reproducible from this repository.** There is no benchmark script, no harness, no recorded results. `vanta_logs.txt` (561 lines) is system logcat noise from app install/launch and contains **zero** `VantaQueryEngine` lines. The engine *does* time itself (`std::chrono` in `search_images`), so the number was probably read off logcat — but nothing here substantiates it | — | **E** | 🔴 **Critical** — a precise unbacked number on a resume project |

---

## C. The three architectural issues worth leading with

These are the ones that make you look like an engineer rather than someone defending a demo.

### C1. The Intent Builder can delete the meaning of the query

After entity spans are stripped, the remaining text goes through `IntentBuilder::buildIntent`, which scans for a word
in `eventsMap` (8 entries), `placesMap` (10 entries), `peopleMap` (19 entries) and **emits only those three slots**:

```cpp
std::string coreSubject = eventIntent;
if (coreSubject.empty() && !peopleIntent.empty()) coreSubject = peopleIntent;
std::string result = coreSubject;
if (!placeIntent.empty()) result = result.empty() ? placeIntent : result + " " + placeIntent;
return result;   // every unmatched word is gone
```

| Query | Words surviving into CLIP | Lost |
|---|---|---|
| `a dog running on grass` | `a dog running on grass` (no map hit → empty intent → raw fallback) | nothing ✅ |
| `my dog at the beach` | **`at a beach`** | **`dog`** ❌ |
| `red car in a park` | **`in a park`** | **`red car`** ❌ |
| `sunset at the beach` | **`at a beach`** | **`sunset`** ❌ |

The fallback `if (clip_caption.empty()) clip_caption = cleaned;` only rescues the case where *nothing* matched. A
partial match is worse than no match. **This is why `useIntent` exists as a user-facing toggle** (`setSearchOptions`) —
and it is the single highest-value one-line fix in the repo: append the unmatched residue instead of discarding it.

### C2. The JNI design is actually good — know why, because it looks wrong

The interviewer's reflex is "you cross JNI per image, that's slow." You don't:

- **Ingestion:** one `startStoring` call for the *entire* scan. For 100k files that is **1 crossing**, not 100k.
- **Indexing:** `generateEmbeddingsNative` is **one** crossing for the whole job. The loop over images, all ONNX
  inference, all SQLite writes and all clustering happen entirely in C++. Progress is polled separately via
  `getIndexProgressNative`, which returns a tiny JSON string.
- **Search:** one crossing in, one JSON string out.

So Vanta's JNI cost is O(1) per *operation*, not O(n) per *item*. The real cost is inside `startStoring`: ~12
`GetObjectField`/`GetStringUTFChars` pairs × N files in a single call — for 100k files roughly **1.2M JNI accessor
calls and ~500k UTF-8 conversions**, plus the Kotlin side materialising the whole `List<FileMeta>` in the JVM heap
before the call. That is the thing to criticise, and the fix is to restore the 200-file batching that `scanPhone`
already offers, or move to a primitive-array / `ByteBuffer` columnar layout.

### C3. There is a real data race on the CLIP tokenizer

In `searchImagesNative`, CLIP-session creation and NER creation are both inside `std::lock_guard<std::mutex>
lock(g_session_mutex)` scopes. The tokenizer load is **not**:

```cpp
{
    std::lock_guard<std::mutex> lock(g_session_mutex);
    ...
    if (!g_tokenizer) g_tokenizer = new CLIPTokenizer();
}                                  // <-- lock released here

if (!g_tokenizer->is_loaded()) {   // <-- UNSYNCHRONISED
    ...
    g_tokenizer->load(vocab_path, merges_path);   // mutates vocab_ and bpe_ranks_
}
```

Each `searchImages` call from JS spawns a **raw `Thread {}`** in Kotlin with no serialisation. Two searches issued
before the first finishes loading will both enter that block and concurrently mutate the same
`std::unordered_map` — undefined behaviour, and the realistic symptom is a crash or a corrupted vocabulary on the
first two rapid searches after a cold start. `Ort::Session::Run` itself *is* thread-safe, so the sessions are fine; the
tokenizer is not.

---

## D. Risk-ranked summary — what to rehearse

| # | Finding | Severity | Your posture |
|---|---|---|---|
| 1 | README claims document indexing; none exists | 🔴 | "That line is aspirational and should come out of the README. Documents are discovered and stored, not embedded." |
| 2 | "DistilBERT" is actually 2-layer BERT-tiny | 🔴 | Lead with the truth: "a 4.4M-parameter 2-layer BERT-tiny token classifier — I chose tiny deliberately, see §12." |
| 3 | ~280 ms latency has no harness behind it | 🔴 | "Measured informally off logcat; I don't have a reproducible harness, and I wouldn't defend the number without one." |
| 4 | Intent Builder drops unmapped words | 🔴 | Present as a bug you found, with the one-line fix. |
| 5 | iOS is a stub; project is Android-only | 🔴 | Never say "cross-platform" unprompted. Say "the C++ core is portable; only Android is wired up." |
| 6 | `entity_memberships.score` never written | 🔴 | See A1. |
| 7 | Merge pass cascades away graph edges | 🟠 | See A3. |
| 8 | `entity_id = -1` tombstone fails on FK | 🟠 | See A2. |
| 9 | Tokenizer load race | 🟠 | See C3. |
| 10 | Docs claim FP16 everywhere; face models are INT8 | 🟠 | Know both and why they differ (§08). |
| 11 | No `busy_timeout` anywhere → SQLITE_BUSY on writer contention | 🟠 | See [18](./18_FAILURE_MODES_AND_CONSISTENCY.md). |
| 12 | CI has never run (wrong directory + gitignored build dir) | 🟠 | "The workflow is written but mis-located; it's not wired up." |

---

## E. What is genuinely solid — lead with these

Do not let the list above make you defensive. These are real, and they are the parts worth being proud of:

- **The SCRFD anchor decoder is hand-written and correct.** Strides 8/16/32, 2 anchors per location, distance-to-box
  decoding scaled by stride, 5 keypoints decoded the same way, sigmoid applied only when outputs look like logits,
  IoU-0.4 NMS, then letterbox coordinates inverted back to the original image. Getting that right from output tensor
  shapes alone is not trivial.
- **ArcFace alignment is textbook-correct**: the canonical 5-point ArcFace landmark template
  `(38.2946, 51.6963) …` with `estimateAffinePartial2D` (similarity transform, not full affine — correct, since you
  want rotation+scale+translation only) into 112×112.
- **The hybrid retrieval idea is sound and correctly motivated.** Pure vector search genuinely cannot answer
  "me and mom at the beach"; the graph-then-rerank decomposition is the right shape.
- **Batching across JNI is done properly** (§C2) — better than most bridge code.
- **WAL mode + one writer + read-only reader connections** is the right concurrency model for this workload.
- **The precision split is defensible**: FP16 for the transformer (CLIP), INT8 dynamic for the convnets (face) — that
  is roughly what you'd choose on purpose.
- **The typo→NER→resolve→graph→rerank pipeline degrades gracefully at every stage.** NER failing to load is non-fatal;
  NER returning nothing falls back to word-by-word fuzzy resolution; empty intersection falls back to union; no
  entities at all falls back to global KNN. That layering is deliberate and good.
