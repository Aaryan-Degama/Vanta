# 25 — Hostile Interview Mode

> Every sentence below is something you might casually say. Each is followed by the attack it invites and the answer
> that survives it. **The pattern throughout: concede the specific, defend the reasoning, offer the fix.**

---

## "We use C++ for performance."

> **⚔ Which operations actually require C++? Name them.**

**Weak answer:** "ML inference is compute-heavy."
**Strong answer:**

> "Honestly, *performance* isn't the load-bearing reason, and I'd rather give you the real one.
>
> The forcing function is **sqlite-vec**. It's a loadable C extension, and Android's `android.database.sqlite` API
> doesn't expose `sqlite3_enable_load_extension` — the platform SQLite is built without extension loading. So there
> was no path to vector search through the system database at all. I compile the SQLite amalgamation into
> `libvanta.so` and statically link sqlite-vec with `SQLITE_CORE` plus `sqlite3_auto_extension`. That single
> constraint makes native code mandatory, independent of speed.
>
> Then ONNX Runtime and OpenCV are C/C++ libraries whose Java bindings are auto-generated JNI wrappers — so with
> Kotlin I'd be crossing JNI anyway, just with less control over tensor memory.
>
> The genuine performance argument is narrower than 'C++ is fast': it's about **allocation behaviour in a
> multi-hour loop**. Per image I decode, resize, normalise, pack NCHW and convert to FP16. In C++ those are buffers
> I own. On the JVM each intermediate is a GC-visible allocation, and hours of allocating full-resolution `Bitmap`s
> is exactly the workload that produces GC pauses and OOM.
>
> And I'd volunteer the cost: I have two memory-safety bugs — a use-after-free on the query analyzer and a data race
> on the tokenizer — plus about fifteen discarded `sqlite3_step` return codes. Those are precisely the class of bug
> Rust would have made impossible, and if I were starting today with Rust fluency I'd give it serious thought."

---

## "We use CLIP for semantic search."

> **⚔ Explain the contrastive objective, and why an image vector and a text vector can be compared at all.**

> "Batch of N image–caption pairs. Encode all N of each, L2-normalise, form the N×N cosine matrix scaled by a
> learned temperature. The diagonal is the N true pairs; the N²−N off-diagonal entries are in-batch negatives. The
> loss is symmetric cross-entropy — each row is an N-way classification over captions, each column an N-way
> classification over images.
>
> **Why the spaces are comparable is the point of the objective, not a separate step.** Both encoders are trained
> *jointly*, and the only way the loss can go down is by moving matching image and text embeddings together and
> pushing non-matching ones apart. The shared space isn't aligned after the fact — it's what the loss constructs.
> The two projection matrices at the ends of the encoders are the learned maps into it.
>
> Two properties I actually depend on: normalising makes cosine equal the dot product, which is why every vector I
> store is unit-norm and ranking is a dot product; and the space has **no absolute calibration**, so a cosine of
> 0.31 doesn't mean '31% relevant'. Scores compare across candidates for one query, never across queries — which is
> exactly how I use them, and why I can't set a global relevance threshold."

> **⚔ Then why is your CLIP retrieval probably worse than it should be?**

> "Because of a preprocessing bug I found auditing my own code. I do `cv::resize(img, 224×224)` — an anisotropic
> squash. Reference CLIP resizes the shortest side to 224 then centre-crops. A 16:9 photo gets stretched 1.78×
> horizontally. That's a train/inference mismatch degrading **every embedding in my database**, silently. It's the
> first thing I'd fix, and it's also the best argument for the golden tests I don't have — twenty fixture images
> embedded in Python versus my C++ path, asserting cosine above 0.99, would have caught it in an afternoon."

---

## "We cluster faces."

> **⚔ What happens when the same person's embeddings form multiple modes?**

> "My system handles it badly and it's my deepest flaw.
>
> Pose is the dominant axis of variation — frontal versus profile can exceed the difference between two people. Add
> age, glasses, facial hair, lighting. The distribution is genuinely **multi-modal**.
>
> I represent a person with one vector. The mean of a bimodal distribution sits *between* the modes, where **no real
> face of that person lives**. So a new frontal face is close to the frontal mode but far from my representation —
> it fails my threshold and founds a spurious identity. And that between-modes region is also where other people's
> embeddings sit, so a stranger can be closer to my representation than any real face of that person. **Too far from
> the right faces and too close to the wrong ones at the same time.**
>
> The fix is Apple's: canonical exemplars. Keep a handful of diverse, quality-gated embeddings per person and score
> a face as the **max** over the set, not distance to an average. For me that's a schema change — `person_centroids`
> becomes `person_exemplars`, `k=1` becomes `k=5` with a group-by max. Ten exemplars is 20 KB per person."

---

## "We use a centroid."

> **⚔ Why can a centroid destroy identity structure?**

> "Three ways, and mine has a fourth.
>
> **It averages away the modes** — that's the multi-modality argument above.
>
> **It's not robust.** One outlier — a misdetection, a face in a poster, a reflection — is admitted at full weight
> and permanently shifts the prototype. A medoid or a quality-weighted mean resists that; a plain mean doesn't.
>
> **It destroys the confidence signal.** Distance to a mean tells you nothing about whether the identity is tight or
> diffuse. With exemplars, the *spread* of similarities is itself information.
>
> **And mine isn't even a centroid** — I'd rather say that than let you find it. It's a normalised exponential
> moving average, `0.9·old + 0.1·new`, re-normalised each time. My own documentation describes a running mean and
> calls EMA the 'alternative'; the code implements the alternative. That makes identity **order-dependent** —
> re-index the same library in a different order and you get different people — and recency-biased, since after
> about thirty assignments the earliest faces contribute under 4%. EMA was a defensible instinct for appearance
> drift, but I shipped it without the guard rails EMA needs: no quality weighting, no anchor to the founding
> exemplar, and a docs/code mismatch that shows I wasn't deliberate about it."

---

## "We use GraphRAG."

> **⚔ Are you actually implementing GraphRAG?**

> "**No**, and I'd correct that word before you had to.
>
> GraphRAG is a specific Microsoft Research system: an LLM reads a text corpus and extracts a typed knowledge graph,
> Leiden community detection builds a hierarchy, an LLM summarises each community at every level, and queries are
> answered by generating text over either a local subgraph or a map-reduce over community summaries.
>
> **I have no LLM, no community detection, no summarisation, and no generation. There is no 'G' in my RAG** — I
> return photographs.
>
> What I took is the architectural claim, and I think it's the right one: *don't make one embedding space answer
> every kind of question*. Extract entities from unstructured input, resolve them to canonical graph nodes, use
> structure to select candidates, and use vector similarity only to rank within them. GraphRAG argues that for
> multi-hop questions over text; I'm arguing it for identity-constrained queries over photos.
>
> And to be concrete about the gap: my 'graph' is a co-occurrence table with two indexes and a **one-hop** lookup.
> `get_neighbors` exists but only feeds the People page — it never touches the ranker. Calling that GraphRAG would
> be a claim you could dismantle in thirty seconds."

---

## "It's cross-platform."

> **⚔ Which parts are actually cross-platform today?**

> "Essentially none of the shipping product, and I shouldn't use that phrase.
>
> **iOS is a 15-line Swift stub.** Two functions: `helloFromKotlin` returns 'hi from swift', and `helloFromCpp`
> returns 'hi from swift (C++ not implemented on iOS yet)'. No engine, no models, no database.
>
> Android is **arm64-v8a only** — `abiFilters 'arm64-v8a'`, and the bundled ONNX Runtime is aarch64. That also means
> it won't run on an x86_64 emulator.
>
> What *is* true: the C++ core is portable by construction. Nothing in the query engine, clustering, graph or
> retrieval code touches JNI — that's confined to one adapter file. A port is a new adapter, not a new engine.
>
> But I'd flag the hard part, because it isn't the engine: on iOS, `PHPhotoLibrary` hands you `PHAsset`s, not file
> paths, and my engine takes a path and calls `cv::imread`. So the port forces the bytes-or-file-descriptor redesign
> I should already be doing on Android for scoped-storage correctness. The accurate phrase is **'portable by
> construction, single-platform in practice'**."

---

## "Search takes about 280 milliseconds."

> **⚔ Prove it.**

> "I can't, and I'd rather say so. That came from reading my own timing logs on my device — `search_images` brackets
> the pipeline with `std::chrono` and logs the elapsed time. But **there's no harness, no query set, and no recorded
> results in the repo**, so it's an informal observation, not a benchmark.
>
> I'd also stop quoting one average, because it conflates two paths with completely different cost models. An
> entity-restricted search is dominated by the CLIP text encoder — a fixed cost, independent of library size. A
> global KNN is a brute-force scan, **linear** in library size. Averaging them hides the only interesting fact.
>
> For a number I'd defend: fixed query set, named device, p50/p95 reported per path. I'd also expect the number to
> get *worse* at scale for a reason unrelated to KNN — `init_face_schema` creates no indexes, so my face
> verification query full-scans a table with one row per face in the library, once per result."

---

## "We index documents and videos."

> **⚔ Show me the code.**

> "You won't find it, because it doesn't exist, and that line needs to come out of my README.
>
> Documents and videos are *discovered* — the scanner classifies eleven document extensions and seven video ones and
> writes `files` rows. But the indexer selects `WHERE filetype = 'picture'`. There's no text extraction, no chunking,
> no keyframe extraction, and no text embedder anywhere in the repo. The `document_chunks` and `document_vec` tables
> exist only in `DBschema.sql`, which is a design document that is **never executed** — the live schema is built in
> C++ by `initialize_database`.
>
> That's a straightforward overclaim on my front page and I'd fix the README rather than defend it."

---

## "The NER model is DistilBERT."

> **⚔ Is it?**

> "No, and the README is wrong. It's **BERT-tiny** — `BertForTokenClassification`, **2 encoder layers, hidden size
> 128**, 4.37 million parameters, 16.94 MB. DistilBERT is 6 layers at hidden 768, about 66 million parameters,
> roughly 265 MB. Fifteen times the size.
>
> The fastest way to catch it is in my own code: DistilBERT **has no `token_type_ids` input** — it removed segment
> embeddings entirely. `ner.cpp` passes three inputs including `token_type_ids`. So the calling code proves the
> model isn't DistilBERT.
>
> But tiny is the right choice and I'd defend that separately: it's on the **search critical path**, it ships in an
> APK already carrying ~300 MB of CLIP, and the task is four classes over about ten words of near-templated text.
> The thing I *can't* defend is that I have **no held-out F1 measurement**, so I can't tell you how much accuracy
> the small model costs."

---

## "The indexing is efficient."

> **⚔ Is it?**

> "No. Let me give you the three biggest wastes before you find them.
>
> **Every image is read from disk and JPEG-decoded twice** — once by `cv::imread` for CLIP and again by
> `ifstream` + `cv::imdecode` for faces. For a 12 MP photo that's **[ESTIMATED]** 50–150 ms wasted per image and 2×
> the transient memory.
>
> **Batch size is always 1**, even though both the CLIP and ArcFace ONNX graphs declare **dynamic batch
> dimensions**. Batching 4–8 is roughly 1.5–2.5× throughput I'm leaving on the table.
>
> **I write a 320×320 JPEG for every detected face.** At 100k photos that's about 4.5 GB — roughly eight times my
> entire database — to render avatars where exactly one crop per *person* is ever displayed.
>
> Fix those three and I'd expect 3–5×, which takes a 100,000-photo library from about fourteen hours to three. I'd
> also be careful not to overclaim on threading: ORT already saturates the cores during inference, so pipelining
> recovers the non-inference time, maybe 1.3–1.6×, not a linear speedup."

---

## "It's private, so it's secure."

> **⚔ Those aren't the same thing.**

> "They're not, and the distinction matters here.
>
> The *privacy* claim is strong and architecturally enforced: no networking code anywhere, no `INTERNET` permission,
> no analytics, no crash reporter. A compromised dependency has no egress. That's a property of the build, not a
> promise.
>
> But **on-device is not encrypted-at-rest**. `vanta.db` is plaintext in app-private storage, protected only by the
> Android sandbox and file-based encryption while locked. And it holds more than metadata: ArcFace embeddings —
> which are **biometric data under GDPR Article 9 and BIPA regardless of being floats** — plus user-assigned names,
> relations and ages, a co-occurrence social graph, and thousands of **aligned face JPEGs** in one directory.
>
> The honest framing is that I created a concentrated biometric dataset that didn't previously exist. The photos
> were always there; a queryable index of every face, clustered by identity and labelled 'Mom', was not.
>
> My top fix is one line: **`android:allowBackup="false"`**. If that defaults to true, the face database goes to
> Google Drive via Auto Backup — which would silently contradict the entire premise. After that: stop persisting
> face crops, `PRAGMA secure_delete`, replace a `system("rm -rf " + path + "/*")` call with `std::filesystem`, and
> SQLCipher with a Keystore key gated on device unlock."

---

## "The clustering thresholds are tuned."

> **⚔ Tuned against what?**

> "Against my own photo library, by trying a few values. That's a calibration procedure with a **sample size of one
> family**, and I'd call it that rather than 'tuned'.
>
> And there's an internal inconsistency you can spot without any benchmark. My thresholds are L2 distances on
> unit vectors, so converting via $\cos = 1 - L2^2/2$: a single face needs **cos 0.82** to join an existing person,
> but two entire identities merge at **cos 0.50**. So my own code asserts that 0.55 is insufficient evidence to
> attach one face to a person, while simultaneously asserting it's sufficient to fuse two whole identities — the
> irreversible operation. Both can't be right.
>
> Worse, the merge loop updates the centroid in place and keeps scanning, so merges **chain**: A into B, then the
> blended A′ into C, and so on. A single greedy pass with a loose threshold can cascade several people into one.
>
> The fix that doesn't need labels: calibrate per identity from the library's own statistics. Each identity's
> members give me an intra-class distribution, and **co-occurring faces give me free known-impostor pairs** —
> two people in the same photo are definitely different. That's a real, on-device, label-free calibration
> procedure, and the data is already in my database."

---

## "The graph makes search fast."

> **⚔ Does it? Show me where it doesn't.**

> "It does for the queries it covers, and I can be precise about the boundary.
>
> For a two-person query it's a single primary-key probe into a materialised `(entity_a, entity_b, file_id)` table —
> O(1), returning ~40 candidates out of 100,000. That's three orders of magnitude better than the brute-force scan.
>
> Where it *doesn't* help: any query with no resolvable entity falls through to a global KNN that is linear in
> library size. And I have a bug that quietly degrades the fast path — my merge pass deletes an entity without
> transferring its co-occurrence edges, and the foreign key cascades them away. I confirmed that by running the real
> schema. So after the merge pass, two-person lookups for merged identities return nothing and fall back to the
> membership-intersection path. Still correct, but it converts an O(1) probe into two scans plus a set intersection,
> and it silently loses the co-occurrence history.
>
> The graph also isn't in the *ranking* at all — `co_occurrence_count` is computed, stored, and never read by the
> ranker. And the membership score that the ranker does read is never written, so it's always 1.0, which makes the
> people-only ranking path effectively unordered."

---

## "The system is well tested."

> **⚔ Is it?**

> "No. Four Jest files, about twelve assertions, all TypeScript. **Zero tests for the 4,500 lines of C++, the 1,200
> lines of Kotlin, the JNI boundary, or any model.** No retrieval metrics, no face metrics, no benchmark, no
> regression baseline.
>
> There's a CI workflow, and it has never run — it's at `vanta_app/.github/workflows/`, but GitHub Actions only
> reads `.github/workflows/` at the repository root. And even relocated, its native job does `cd android &&
> ./gradlew`, where `android/` is gitignored Expo prebuild output that nothing regenerates.
>
> The cost is concrete, not abstract: **three of the bugs in my own audit** — a column that's never written, a
> foreign-key rejection that's silently swallowed, and a cascade that deletes graph edges — would all have been
> caught by a single integration test that indexed ten fixture photos and asserted on the resulting database. The
> tests I have are the tests that were easiest to write, not the ones that would have found my bugs."

---

## The meta-question

> **⚔ You've just told me a dozen things are wrong with your own project. Why should I be impressed?**

> "Because the alternative is that you find them and I don't have an answer.
>
> Every issue I've listed I found by auditing my own repository — and three of them I confirmed by **executing my
> real schema against SQLite** rather than reasoning about it, because I wanted to know whether I was right. The
> membership score really is always 1.0. The foreign key really does reject the tombstone. The merge really does
> cascade the graph edges away.
>
> What I'd want you to take from this isn't a list of bugs. It's that I can tell you **which** bug matters most and
> why: the identity representation, because it's the only one whose blast radius is the product's headline feature,
> and because a false merge is the one error a user can't undo. Everything else is a preprocessing fix, a missing
> index, or a line of SQL.
>
> And I'd note what I *didn't* conclude: that the architecture is wrong. The hybrid design — a structured identity
> index plus a semantic vector index, with a planner that routes each part of a query to the index that can answer
> it — I'd build again. The problems are in the quality layer, not the shape."
