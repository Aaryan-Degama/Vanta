# 15 — SQLite & sqlite-vec

## What Vanta stores [VERIFIED-REPO — the *runtime* schema, not `DBschema.sql`]

> ⚠️ `DBschema.sql` at the repo root is a **design document that is never executed**. The live schema is created by
> `DBoperations.cpp::initialize_database` → `init_face_schema` → `init_graph_schema`. They differ substantially
> (see [01 §B3](./01_VANTA_REALITY_CHECK.md)).

| Table | Kind | Purpose |
|---|---|---|
| `files` | ordinary | every discovered media file |
| `clip_vec` | **`vec0` virtual** | `file_id INTEGER PRIMARY KEY, embedding FLOAT[512]` |
| `entities` | ordinary | person clusters (+ `relation`, `age`, `location` via runtime migration) |
| `person_centroids` | **`vec0` virtual** | `entity_id INTEGER PRIMARY KEY, embedding FLOAT[512]` |
| `face_detections` | ordinary | one row per detected face; bbox, `det_score`, `aligned_crop_path`, `entity_id` |
| `face_vec` | **`vec0` virtual** | `detection_id INTEGER PRIMARY KEY, embedding FLOAT[512]` |
| `entity_memberships` | ordinary | person → file edges |
| `entity_relations` | ordinary | person ↔ person co-occurrence edges |
| `entity_relation_files` | ordinary | materialised (pair → file) index |

**Not created at runtime:** `document_chunks`, `document_vec`. They exist only in `DBschema.sql`.

### Indexes actually created

```sql
CREATE INDEX idx_files_filetype   ON files(filetype);
CREATE INDEX idx_files_mtime      ON files(mtime_unix);
CREATE INDEX idx_files_status     ON files(status);
CREATE INDEX idx_files_face_count ON files(face_count);
CREATE INDEX idx_er_a             ON entity_relations(entity_a);
CREATE INDEX idx_er_b             ON entity_relations(entity_b);
CREATE INDEX idx_erf_file         ON entity_relation_files(file_id);
CREATE INDEX idx_erf_pair         ON entity_relation_files(entity_a, entity_b);
-- plus implicit: files.abs_path UNIQUE, and every PRIMARY KEY
```

**Analysis you should be able to give:**

| Index | Justified? |
|---|---|
| `idx_files_status` | ✅ The indexing hot loop is `WHERE filetype='picture' AND status='pending' LIMIT 10` — run once per batch. **A composite `(status, filetype)` would be better than either single-column index** since the predicate is conjunctive |
| `idx_files_face_count` | ⚠️ Created for "exclusive entity queries" per the schema comment — **and never used by any query**. Dead index, paying write cost for nothing (or, better framed: a feature that's half-built) |
| `idx_erf_pair` | ⚠️ **Redundant.** `entity_relation_files` has `PRIMARY KEY (entity_a, entity_b, file_id)`, which SQLite backs with an index whose leading columns are exactly `(entity_a, entity_b)`. This duplicate costs write amplification and space for no read benefit |
| `idx_er_a` / `idx_er_b` | `idx_er_a` is likewise partly redundant with the PK `(entity_a, entity_b)`; `idx_er_b` is genuinely needed for the `OR entity_b = ?` half of `get_neighbors` |
| **Missing:** `entity_memberships(entity_id)` | 🔴 `DBschema.sql` declares `idx_memberships_entity` — **`face_DB.cpp` never creates it.** But the PK is `(entity_id, file_id)`, whose leading column is `entity_id`, so the PK index serves the lookup. Fine by accident |
| **Missing:** `face_detections(file_id)`, `face_detections(entity_id)` | 🔴 `DBschema.sql` declares both; **`init_face_schema` creates neither.** `cluster_faces_for_file` runs `WHERE d.file_id = ? AND d.entity_id IS NULL` and `verify_entities_in_file` runs `WHERE file_id = ? AND entity_id IN (…)` — **once per search and once per indexed file, against a table with one row per face in the entire library**. At 100k photos with ~2 faces each that's a 200,000-row full table scan on the search path. **This is the most consequential missing index in the schema and an excellent thing to volunteer** |

---

## Why SQLite?

### The concepts

**Embedded** — SQLite is a C library linked into the process, not a server. No sockets, no daemon, no auth, no
separate lifecycle. `sqlite3_open()` opens a file.

**ACID** — Atomicity (a transaction applies wholly or not at all), Consistency (constraints hold), Isolation
(concurrent transactions don't see each other's partial work), Durability (committed data survives a crash).

**WAL (Write-Ahead Logging)** — Vanta sets `PRAGMA journal_mode = WAL`. Instead of copying original pages into a
rollback journal and writing in place, writers **append** new pages to a `-wal` file; readers consult the WAL plus
the main DB. The decisive property:

> **In WAL mode, readers do not block the writer and the writer does not block readers.**

That is exactly Vanta's workload: a long-running indexing writer plus interactive read-only searches. In the older
rollback-journal mode, a search during indexing would either block or fail. **WAL is arguably the single most
important line in the whole data layer**, and `initialize_database` sets it correctly.

Supporting details: a `-shm` shared-memory file coordinates readers; the WAL is periodically **checkpointed** back
into the main DB (automatic at ~1000 pages by default); WAL requires the DB to be on a filesystem supporting shared
memory, which app-private storage is.

**`PRAGMA foreign_keys = ON`** — off by default in SQLite for backwards compatibility. Vanta turns it on, which is
correct and is also why the `entity_id = -1` tombstone fails and why the merge pass cascades edges away
([01 §A2, §A3](./01_VANTA_REALITY_CHECK.md)). Note that **the pragma is per-connection**, and Vanta's many
`sqlite3_open_v2(..., SQLITE_OPEN_READONLY, ...)` connections never set it — harmless for readers, but worth knowing.

### Why not the alternatives?

| Option | Verdict |
|---|---|
| **PostgreSQL / MySQL** | Client-server. Requires a running daemon. Not deployable inside an Android app. Instantly disqualified |
| **Room** | An ORM/compile-time-checked wrapper **over Android's SQLite**. Two fatal problems: (a) it's a JVM API, and Vanta's DB access is entirely in C++; (b) Android's bundled SQLite **cannot load extensions**, so `sqlite-vec` is impossible through it. See below |
| **Realm** | Object DB, good mobile story, but: no vector search, no SQL, and it would be a second storage engine alongside whatever holds the vectors |
| **DuckDB** | Excellent analytical engine, has a `vss` extension, but it's column-store OLAP — the wrong shape for Vanta's point lookups and small transactional writes, and much heavier on mobile |
| **RocksDB / LevelDB** | Fast embedded KV store. You'd hand-build every index, every join, every transaction boundary, and all the relational query logic. No SQL, no vectors |
| **LMDB** | Same objection |
| **Files + an in-memory index** | No durability, no crash consistency, and you'd rebuild the whole index on every launch |

### The decisive argument: Android's SQLite can't load extensions

This is the point to lead with, because it explains why Vanta compiles its own SQLite rather than using the one
Android already ships:

> "`sqlite-vec` is a **loadable C extension**. Android's `android.database.sqlite` API does not expose
> `sqlite3_enable_load_extension`, and the platform SQLite is built without extension loading. So there was no path
> to vector search through the system database at all.
>
> Vanta compiles the SQLite **amalgamation** (`sqlite3.c`) directly into `libvanta.so`, and statically links
> sqlite-vec alongside it with `#define SQLITE_CORE 1` plus
> `sqlite3_auto_extension((void(*)())sqlite3_vec_init)` before every open. `SQLITE_CORE` makes the extension link
> against the amalgamation's symbols directly instead of going through the `sqlite3_api_routines` function table,
> and `auto_extension` registers the init hook so `vec0` is available on every connection automatically.
>
> That also has a side benefit: I control the SQLite version and compile flags, so I'm not at the mercy of whatever
> SQLite build a given Android version shipped."

---

## Why sqlite-vec?

**sqlite-vec** (<https://github.com/asg017/sqlite-vec>) is a SQLite extension providing `vec0` virtual tables for
storing and searching float vectors, written in portable C with no dependencies.

```sql
CREATE VIRTUAL TABLE clip_vec USING vec0(file_id INTEGER PRIMARY KEY, embedding FLOAT[512]);
INSERT OR REPLACE INTO clip_vec(file_id, embedding) VALUES (?, ?);   -- blob of 512 floats
SELECT file_id, distance FROM clip_vec WHERE embedding MATCH ? AND k = 30;
```

### What Vanta actually uses [VERIFIED-REPO — this matters]

| Feature | Used? | Detail |
|---|---|---|
| `vec0` virtual tables | ✅ | three of them |
| `FLOAT[512]` (float32) | ✅ | all three; **no `int8` or `bit` quantisation** |
| KNN via `MATCH ... AND k = ?` | ✅ | `clip_vec` (k=30) and `person_centroids` (k=1) |
| Default **L2** distance metric | ✅ | **no `distance_metric=cosine` is specified anywhere** — this is why the clustering thresholds are L2 values (0.6, 0.75, 1.0) and need converting via $\cos = 1 - L2^2/2$ |
| Metadata / auxiliary columns | ❌ | Vanta joins back to `files` by id instead |
| Partition keys | ❌ | |
| **ANN indexes (DiskANN / IVF)** | ❌ | **`sqlite-vec-diskann.c`, `sqlite-vec-ivf.c`, `sqlite-vec-ivf-kmeans.c`, `sqlite-vec-rescore.c` are all in `thirdparty/` and none is in the CMake source list.** Only `sqlite-vec.c` is compiled |
| Scalar/binary quantisation | ❌ | |

**So every KNN in Vanta is an exhaustive brute-force scan.** State this plainly — it's a defensible choice at this
scale and pretending otherwise is the kind of thing that gets caught.

### Brute force vs ANN — the honest numbers

For `clip_vec` with *N* files, a global KNN is *N* × 512 multiply-adds.

| N | FLOPs per query | **[ESTIMATED]** time at ~1 GFLOP/s scalar single-core |
|---|---|---|
| 1,000 | 1.0 M | ~1 ms |
| 10,000 | 10.2 M | ~10 ms |
| 100,000 | 102 M | **~100 ms** |
| 500,000 | 512 M | **~500 ms** |

*(Derivation: N × 512 × 2 FLOPs. The constant is a rough mobile-CPU figure — treat the shape as reliable and the
absolute numbers as an estimate, not a measurement.)*

> **The key architectural insight — say this:** *"Brute force is linear in library size, but **only the global-KNN
> fallback path ever runs it**. Any query that resolves an entity goes through the graph first and scores 40
> candidates instead of 100,000. So the O(N) cost is confined to the queries that have no structural constraint to
> exploit — which is exactly the right place for it. That's the scaling argument for the hybrid design, not just an
> accuracy argument."*

**When ANN becomes necessary:** somewhere around 100k–500k vectors, if global semantic search latency matters. The
options then are (a) compile in sqlite-vec's DiskANN/IVF (the source is literally already in the repo), (b) binary-
quantise to a `bit[512]` index for a Hamming-distance first pass and rescore the top few hundred in float32 — an
~8–32× speedup for a small recall loss, and sqlite-vec supports it, or (c) partition by time and search recent
photos first.

### Comparison with dedicated vector databases

| System | Type | Fatal problem for Vanta |
|---|---|---|
| **FAISS** | C++ library | Genuinely the best ANN library. But it is **an index, not a database**: no persistence semantics, no transactions, no relational joins. You'd maintain a FAISS index **and** SQLite and keep them consistent by hand — including across crashes. Also a large dependency to cross-compile for Android |
| **Chroma** | Python/server | Not embeddable in an Android app |
| **Qdrant** | Rust server | Server. There's an embedded mode, but it's a second storage engine and a large binary |
| **Milvus** | Distributed server | Absurd for a phone |
| **Pinecone / Weaviate** | Managed cloud | 🔴 **Violates the core product requirement.** Sending face embeddings to a cloud service is precisely what Vanta exists not to do |
| **ObjectBox vector search** | Embedded, mobile-first | A genuine alternative and worth naming — but it's a whole different DB, so you lose SQL and the relational graph |
| **sqlite-vec** ✅ | SQLite extension | — |

### The decisive argument: one store, one transaction

> "The thing that makes sqlite-vec the right answer isn't speed — FAISS would beat it on raw ANN throughput. It's
> that **the vectors and the relational data live in the same file, under the same transaction.**
>
> Look at what indexing one photo does: insert a CLIP vector, insert N face detections, insert N face vectors,
> insert/update an entity, update a centroid, insert memberships, upsert graph edges. That's a mix of vector writes
> and relational writes that must be atomic — if the app is killed halfway, I need all of it or none of it.
>
> With FAISS + SQLite I'd have two stores with two independent durability models and no way to commit them together.
> After a crash the FAISS index and the SQL tables could disagree, and I'd need my own reconciliation pass on every
> launch. With sqlite-vec it's one `BEGIN`/`COMMIT` on one file.
>
> And the query side matters just as much: `SELECT file_id, embedding FROM clip_vec WHERE rowid = ?` joined against
> `files` is one SQL statement. With a separate index I'd be shuttling id lists between two systems.
>
> **I traded ANN performance for transactional integrity and query simplicity, and at 10–100k vectors on a phone
> that's clearly the right trade.** The moment brute-force KNN becomes the bottleneck, sqlite-vec's own DiskANN
> and IVF backends are already sitting in my `thirdparty/` directory — I just haven't needed to compile them in."

---

## Storage math

### Per-item costs

| Item | Size | Derivation |
|---|---|---|
| CLIP embedding (float32) | **2,048 B** | 512 × 4 |
| CLIP embedding (float16) | 1,024 B | 512 × 2 |
| CLIP embedding (int8) | 512 B | 512 × 1 |
| CLIP embedding (binary) | **64 B** | 512 bits |
| Face embedding (float32) | **2,048 B** | 512 × 4 |
| Person centroid (float32) | 2,048 B | per *entity*, not per photo — negligible |
| `files` row | ~250 B **[ESTIMATED]** | ~14 columns; `abs_path` dominates at ~100–150 B |
| `face_detections` row | ~80 B **[ESTIMATED]** | 6 ints + a real + a crop path |
| `entity_memberships` row | ~40 B | 2 ints + real + int |
| `entity_relations` row | ~48 B | |
| Face crop JPEG (320×320) | ~25–40 KB **[ESTIMATED]** | one per detected face, on disk |

### Scaling — the worked example

For **100,000 photos** averaging **1.5 faces** each:

```
CLIP vectors      100,000 × 512 × 4 B  =  204,800,000 B  ≈  195 MB
Face vectors      150,000 × 512 × 4 B  =  307,200,000 B  ≈  293 MB
files rows        100,000 × 250 B                        ≈   24 MB
face_detections   150,000 ×  80 B                        ≈   11 MB
memberships       150,000 ×  40 B                        ≈    6 MB
graph edges       ~50,000 ×  48 B                        ≈    2 MB
indexes           ~30% of relational data                ≈   13 MB
                                                    ─────────────
DATABASE TOTAL                                         ≈  544 MB

Face crops        150,000 × ~30 KB                     ≈  4.5 GB  ⚠️
Models on disk    CLIP fp16 ~303 MB + face int8 ~46 MB + NER 17 MB  ≈  366 MB
                  (× 2 — once in the APK, once extracted to filesDir)
```

### 🔴 The finding: face crops dwarf everything

**4.5 GB of 320×320 JPEGs** — roughly **8× the entire database** — to render thumbnail avatars on the People page,
of which only **one per entity** is ever displayed (`get_best_face_crop` returns `LIMIT 1`).

```cpp
// face_DB.cpp::run_face_pipeline — written for EVERY detected face
cv::Mat aligned = face_model.align_face(image, faces[i], 320);
cv::imwrite(VantaConfig::instance().crop_path(std::to_string(file_id) + "_" + std::to_string(i) + ".jpg"), aligned);
```

Fixes, in order of value:
1. **Don't write crops during indexing at all.** Generate the avatar lazily when the People page asks for it — the
   bbox and the source path are already in `face_detections`. Saves ~4.5 GB and one `warpAffine` + one JPEG encode
   per face during the most expensive phase of the app.
2. If you must cache: write **one crop per entity**, refreshed when a better-quality face arrives.
3. Drop 320×320 to 160×160 (~4× smaller) — the People page renders them small.

### Scaling table

| Photos | CLIP vecs | Face vecs (1.5×) | Relational + idx | **DB total** | Crops **[ESTIMATED]** |
|---|---|---|---|---|---|
| 1,000 | 2 MB | 3 MB | 0.5 MB | **~5 MB** | ~45 MB |
| 10,000 | 20 MB | 29 MB | 5 MB | **~54 MB** | ~450 MB |
| 100,000 | 195 MB | 293 MB | 56 MB | **~544 MB** | ~4.5 GB |
| 500,000 | 977 MB | 1.4 GB | 280 MB | **~2.7 GB** | ~22 GB ❌ |

### FP16 / int8 / binary — what quantising the vectors would buy

| Precision | CLIP+face @100k | Saving | Cost |
|---|---|---|---|
| float32 (today) | 488 MB | — | — |
| float16 | 244 MB | **50%** | Negligible accuracy impact for retrieval; sqlite-vec supports `float16` natively |
| int8 | 122 MB | **75%** | Small recall loss; needs per-vector scale/offset |
| **binary (512 bit)** | 30 MB | **94%** | Real recall loss — but as a *first-stage* filter with float32 rescoring of the top few hundred, recall loss is minimal and it's ~8–32× faster |

> **The proposal:** store `face_vec` in float32 (clustering thresholds are sensitive and the sizes are manageable),
> but move `clip_vec` to **float16 for storage plus a binary index for the first-stage scan**. That cuts the
> dominant vector cost roughly in half, makes global KNN ~10× faster, and the rescoring stage keeps accuracy. Both
> are supported by sqlite-vec today.

### Where it breaks first

| Limit | When | Note |
|---|---|---|
| Face-crop disk usage | **~50,000 photos** | 🔴 The first wall, and it's entirely self-inflicted |
| Brute-force KNN latency | ~100k–500k vectors | Only affects the global fallback path |
| `merge_similar_entities` O(n²) | ~5,000 entities | 12.5 M 512-d L2 computations; also re-prepares SQL inside the inner loop |
| Missing `face_detections` indexes | ~50k photos | Full scans on the search path |
| JVM heap during `startStoring` | ~200k files **[ESTIMATED]** | The whole scan is materialised before the single JNI call |
| SQLite file size | never | The theoretical limit is 281 TB |
