# 18 — Failure Modes, Consistency & Recovery

## 1. Failure-mode table

| # | Scenario | What Vanta does today | Severity | Correct design |
|---|---|---|---|---|
| 1 | **Corrupt / undecodable image** | `cv::imread` returns empty → `CLIP_instance::load_img` returns it → `get_embedding` throws `std::runtime_error` → caught by the per-image `catch` → `status='failed'` | 🟢 Handled | ✅ as is, plus record *why* it failed |
| 2 | **HEIC / RAW file** | Scanner accepts `.heic`, `.dng`, `.cr3`, `.nef`, `.arw`, … (17 image extensions). **OpenCV cannot decode most of them** → same path as #1 → `failed` | 🟠 **Silent data loss.** On a modern iPhone-adjacent or DSLR-heavy library this could be a large fraction of photos, and nothing tells the user | Use Android's `ImageDecoder`/`BitmapFactory` (which *does* handle HEIC) in Kotlin and pass pixels/fd to native; or drop the extensions you can't decode from `validDtype` |
| 3 | **File deleted after scan, before indexing** | `imread` fails → `failed`. The row, and any vectors, persist forever | 🟠 | Reconciliation pass; treat `ENOENT` distinctly from "corrupt" |
| 4 | **File deleted after indexing** | 🔴 **Nothing.** The `files` row, CLIP vector, face detections, memberships and graph edges all remain. Search returns it; the UI renders a broken `file://` image | 🔴 | A `DELETE FROM files WHERE abs_path NOT IN (scan)` pass; FK `ON DELETE CASCADE` then cleans face data — but **not `clip_vec`**, which is a `vec0` virtual table and has no FK. That needs an explicit delete |
| 5 | **File modified after indexing** | 🔴 **Nothing.** `INSERT OR IGNORE` discards the new row; `mtime_unix` is stored but never compared. The stale embedding persists | 🟠 | `INSERT … ON CONFLICT(abs_path) DO UPDATE SET mtime_unix=…, status = CASE WHEN excluded.mtime_unix > mtime_unix THEN 'pending' ELSE status END` |
| 6 | **Permission revoked mid-scan** | Cursor returns null → `yieldFromMediaStore` returns → treated as an empty collection. **No error surfaces** | 🟠 The user sees "0 files" and no explanation | Check `checkSelfPermission` and fail loudly. `logMediaPermissions` already computes this and only logs it |
| 7 | **API ≤ 32 device** | No `READ_EXTERNAL_STORAGE` declared → no permission → empty scan, silently | 🔴 See [05](./05_KOTLIN_ANDROID.md) | Declare it with `maxSdkVersion="32"` |
| 8 | **Disk full** | `sqlite3_step` returns `SQLITE_FULL`. **Return value discarded in ~15 places.** `cv::imwrite` returns false → logged. Indexing continues writing nothing | 🔴 **Silent corruption of progress** — files get marked `indexed` whose vectors failed to insert | Check every `sqlite3_step`; abort the run and surface a real error |
| 9 | **`SQLITE_BUSY` (writer vs writer)** | 🔴 **No `busy_timeout` is set on any connection in the entire codebase.** `resetFaceData` and `generateEmbeddings` are both reachable from the UI and both open write connections. The loser gets `SQLITE_BUSY` immediately | 🔴 | `sqlite3_busy_timeout(db, 5000)` — **one line, biggest robustness win in the repo**. Plus an app-level mutex so the two jobs can't overlap |
| 10 | **App killed mid-indexing** | ✅ **Handled well by design.** Progress is the `files.status` column, so the next run resumes from `pending`. WAL guarantees no partial transaction is visible | 🟢 **Lead with this** | — |
| 11 | **App killed mid-`cluster_faces_for_file`** | The whole function is one transaction → rolled back. But `save_face_detection`/`save_face_embedding` in `run_face_pipeline` are **not** in a transaction, so detections can exist with `entity_id IS NULL`. Pass 2 picks them up | 🟢 Self-healing by accident | Wrap `run_face_pipeline`'s writes in the same transaction |
| 12 | **Service restarted by `START_STICKY`** | Android restarts with a **null Intent** → `intent?.action == ACTION_START` is false → nothing happens | 🟠 Indexing does **not** auto-resume; the user must tap again | Persist the job in `SharedPreferences` and handle null intents, or move to `WorkManager` |
| 13 | **Device sleeps / Doze** | Foreground service keeps the process alive; CPU work continues while the screen is off. No thermal or battery guard | 🟠 | Thermal + charge constraints ([16](./16_INDEXING_PIPELINE.md)) |
| 14 | **Model file missing** | `Ort::Session` ctor throws → caught → `load()` returns false → CLIP: search returns `"[]"`; **NER: explicitly non-fatal**, search continues without it | 🟢 Graceful, 🟠 invisible | Structured error to the UI: "search is degraded" |
| 15 | **Face detection finds nothing** | `run_face_pipeline` returns true after setting `face_count = 0`. Correct — a photo with no faces is a valid outcome | 🟢 | — |
| 16 | **Landmark/alignment failure** | `estimateAffinePartial2D` returns an empty `Mat` → `warpAffine` throws. Caught for the 320-px avatar crop; **not caught** in `get_embedding`, so it propagates to the per-image handler and marks the **whole file** `failed` — losing its CLIP indexing too | 🟠 | `if (M.empty()) continue;` |
| 17 | **Vector insertion fails** | `save_clip_embedding` returns false → `status='failed'` ✅. But `save_face_embedding` failure is **logged and ignored** — leaving a `face_detections` row with **no** `face_vec` row | 🟠 Orphan | The `JOIN face_vec` in clustering silently skips it, so it's invisible rather than harmful — but it's an orphan |
| 18 | **Duplicate files** (same photo in two folders) | `abs_path UNIQUE` only dedupes identical paths. Two copies = two rows, two CLIP vectors, two face rows, duplicated memberships and inflated `co_occurrence_count` | 🟠 | Hash-based dedup (size + a partial content hash) |
| 19 | **Two concurrent searches during cold start** | Both enter the unsynchronised tokenizer load → concurrent mutation of `unordered_map` | 🔴 UB | Move the load inside `g_session_mutex` |
| 20 | **Rename an entity during a search** | `rebuild_query_engine_dictionary` does `delete g_analyzer` with no lock | 🔴 Use-after-free | `shared_ptr` + atomic swap |

---

## 2. Database consistency

### What indexing one photo actually writes

```
files:                  UPDATE status='indexed', last_indexed_at     ← in save_clip_embedding
clip_vec:               INSERT OR REPLACE (file_id, embedding)       ← in save_clip_embedding
files:                  UPDATE face_count                            ← in run_face_pipeline
face_detections:        INSERT × N                                   ← in run_face_pipeline   ⚠️ no transaction
face_vec:               INSERT × N                                   ← in run_face_pipeline   ⚠️ no transaction
entities:               INSERT or UPDATE sample_count                ← in cluster_faces_for_file  [TXN]
person_centroids:       INSERT or UPDATE                             ← in cluster_faces_for_file  [TXN]
face_detections:        UPDATE entity_id                             ← in cluster_faces_for_file  [TXN]
entity_memberships:     INSERT OR IGNORE                             ← in cluster_faces_for_file  [TXN]
entity_relations:       UPSERT co_occurrence_count                   ← in update_graph_for_file   [TXN]
entity_relation_files:  INSERT OR IGNORE                             ← in update_graph_for_file   [TXN]
```

### Transaction boundaries [VERIFIED-REPO]

| Function | Transaction? |
|---|---|
| `insert_files` | ✅ explicit `BEGIN`/`COMMIT` around the whole batch |
| `save_clip_embedding` | ❌ two separate autocommit statements (`clip_vec` insert, then `files` update) |
| `run_face_pipeline` | ❌ **each insert autocommits individually** |
| `cluster_faces_for_file` | ✅ `BEGIN` … `COMMIT` |
| `recluster_pending_faces` | ✅ |
| `merge_similar_entities` | ✅ |
| `update_graph_for_file` | ✅ with `ROLLBACK` on error |
| `reset_face_data` | ✅ |

**The gap:** the per-photo work spans **four** independent transactions plus some autocommitted statements. There is
no single "this photo is fully indexed" atomic unit. Crash between them and you get a partially-indexed photo whose
`status` says `indexed`.

### Concrete inconsistencies reachable today

| Inconsistency | How | Detectable? |
|---|---|---|
| `status='indexed'` but no `clip_vec` row | `clip_vec` insert succeeds, `files` update fails (or vice versa — they're separate statements) | `SELECT id FROM files WHERE status='indexed' AND id NOT IN (SELECT file_id FROM clip_vec)` |
| `face_detections` row with no `face_vec` row | `save_face_embedding` fails and is ignored (#17) | `LEFT JOIN face_vec … WHERE v.detection_id IS NULL` |
| `entity_memberships` row for a file with no matching `face_detections` | `merge_similar_entities` copies memberships; `verify_entities_in_file` exists **precisely to defend against this** | the verification query itself |
| Orphan `entity_relations` | 🔴 Actually the opposite: the merge pass **cascade-deletes** them. Verified by execution — [01 §A3](./01_VANTA_REALITY_CHECK.md) | count edges before/after merge |
| `clip_vec` rows for deleted files | No cleanup, and `vec0` tables have no FK to cascade | `clip_vec.file_id NOT IN (SELECT id FROM files)` |
| `entity_id IS NULL` forever | The `-1` tombstone is FK-rejected — [01 §A2](./01_VANTA_REALITY_CHECK.md) | count NULLs after reclustering |

### The crucial point about virtual tables

**`vec0` virtual tables do not participate in foreign keys.** `clip_vec`, `face_vec` and `person_centroids` have no
FK relationship to `files` or `entities`, so `ON DELETE CASCADE` does nothing for them. Note that
`merge_similar_entities` gets this right — it issues an explicit
`DELETE FROM person_centroids WHERE entity_id = ?` rather than relying on a cascade. But nothing does the equivalent
for `clip_vec` or `face_vec`, because nothing ever deletes files.

> **This is a genuinely good thing to raise unprompted:** *"My relational integrity is enforced by foreign keys, but
> my three biggest tables are virtual tables that foreign keys don't apply to. So I have ACID on half my schema and
> manual discipline on the other half. Today it's latent because I never delete anything — but the moment I add
> deletion, every vector table becomes an orphan source. I'd add explicit cascade logic and a consistency-check
> pass rather than discover it in production."*

---

## 3. Robust recovery — the design

### Layer 1: make each photo atomic

```cpp
sqlite3_exec(db, "BEGIN IMMEDIATE;", …);     // IMMEDIATE takes the write lock up front,
                                             // so contention fails now rather than at COMMIT
try {
    save_clip_embedding(db, id, emb);
    run_face_pipeline(db, path, id, face_model);
    cluster_faces_for_file(db, id);
    update_graph_for_file(db, id);
    update_file_status(db, id, "indexed");   // ← the LAST thing, inside the transaction
    sqlite3_exec(db, "COMMIT;", …);
} catch (...) {
    sqlite3_exec(db, "ROLLBACK;", …);
    mark_failed(db, id, error_code);          // separate transaction
}
```

Setting `status='indexed'` **last and inside the transaction** makes the status column a truthful record of
completion — which is what the resume logic already assumes.

### Layer 2: retry with classification and backoff

`files.retry_count` already exists and is **never incremented**. Make it real:

```sql
ALTER TABLE files ADD COLUMN fail_reason TEXT;   -- 'decode' | 'io' | 'oom' | 'model' | 'db'
```

| Reason | Policy |
|---|---|
| `decode` (corrupt / unsupported format) | **Permanent.** Never retry. `status='skipped'` |
| `io` (file missing) | Retry once on the next scan, then delete the row |
| `oom` | Retry with reduced-resolution decode |
| `model` (session load failed) | Retry the whole run; not the file's fault |
| `db` (`BUSY`/`FULL`) | Retry with exponential backoff, up to `retry_count = 3` |

### Layer 3: a startup consistency check

Run on app start if a "dirty" flag is set (set at the beginning of indexing, cleared at the end):

```sql
-- 1. files claiming to be indexed with no vector
UPDATE files SET status='pending'
  WHERE status='indexed' AND id NOT IN (SELECT file_id FROM clip_vec);
-- 2. detections with no embedding
DELETE FROM face_detections WHERE id NOT IN (SELECT detection_id FROM face_vec);
-- 3. vectors for files that no longer exist
DELETE FROM clip_vec WHERE file_id NOT IN (SELECT id FROM files);
-- 4. memberships not backed by a real detection
DELETE FROM entity_memberships WHERE (entity_id, file_id) NOT IN
  (SELECT entity_id, file_id FROM face_detections WHERE entity_id IS NOT NULL);
-- 5. entities with no faces left
DELETE FROM entities WHERE entity_id NOT IN (SELECT entity_id FROM face_detections WHERE entity_id IS NOT NULL);
```

### Layer 4: contention and backpressure

- `sqlite3_busy_timeout(db, 5000)` on **every** connection.
- An application-level lock so `resetFaceData` and `generateEmbeddings` cannot run concurrently. Today the UI allows
  both, and there is nothing preventing it.
- Check `sqlite3_step` return codes. `SQLITE_BUSY`, `SQLITE_FULL`, `SQLITE_CONSTRAINT` and `SQLITE_DONE` are four
  very different outcomes and Vanta currently treats them all as "moved on".

### Layer 5: observability

Today the only failure signal is `adb logcat`. Add a `failures` table (file_id, stage, reason, timestamp) and a
Debug-screen surface, so "1,247 of your photos failed to index, 1,190 of them because they're HEIC" is answerable —
which is exactly the class of problem (#2) that is currently invisible.

---

## 4. The two-sentence summary for an interview

> "The thing Vanta genuinely gets right is **resumability**: progress lives in a `status` column rather than in
> memory, so killing the app mid-index loses at most one photo, and WAL means a search during indexing never blocks.
>
> The thing it gets wrong is **error handling discipline** — `sqlite3_step` return codes are discarded in about
> fifteen places, there's no `busy_timeout` anywhere, and per-photo work spans four transactions instead of one. The
> result is that the system fails *silently* rather than loudly: a disk-full or a lock conflict looks exactly like
> success. If I had one afternoon, I'd add `busy_timeout`, wrap each photo in one transaction, and check every
> return code — that's the difference between a demo and something I'd let a user rely on."
