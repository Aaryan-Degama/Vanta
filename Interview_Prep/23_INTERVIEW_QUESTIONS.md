# 23 — Long-Form Interview Questions

> Format: **Q** → **model answer** → **likely follow-up**. These are the questions that take 2–5 minutes to answer
> properly. For 120 short ones, see [24](./24_RAPID_FIRE.md); for adversarial framing see [25](./25_HOSTILE_INTERVIEW.md).

---

## Architecture

### Q1. Walk me through what happens when I type "photos of me and mom at the beach".

**A.** *(see [14](./14_SEARCH_PIPELINE.md) for the full trace — the compressed version:)*

"The string goes from React Native through an Expo async function into Kotlin, which spawns a thread and makes one
JNI call into the C++ engine. Inside, there are eight stages.

First, spell correction — a SymSpell-style delete-neighbourhood index, but over a small domain dictionary: about
forty hardcoded terms plus every person name and relation from my database, so it personalises itself to the user's
library.

Second, NER. A 2-layer BERT-tiny token classifier tags each word as `SELF`, `PERSON`, `RELATION` or `O`. Here
'me' comes back as `B-SELF` and 'mom' as `B-RELATION`. Nothing else is tagged — that's deliberate, because the
graph owns people and CLIP owns everything else.

Third, resolution. 'me' maps to the owner entity the user set in Settings. 'mom' is fuzzy-matched by
Damerau-Levenshtein against both `display_name` and `relation`, within edit distance 2, tie-broken by sample count.

Fourth, the residual text — 'at the beach' — goes through an intent builder into a CLIP caption.

Fifth, candidate selection. Two entities, so it's a single primary-key lookup into a materialised
`entity_relation_files` table: one row per (person A, person B, file) triple. That's O(1) and returns maybe forty
files out of a hundred thousand.

Sixth, reranking. I embed the caption with CLIP's text encoder, fetch each candidate's stored image embedding by
rowid, and take dot products — both vectors are unit-norm so that's cosine similarity directly.

Seventh, I re-verify against `face_detections` that both entities really are in each file, guarding against stale
membership rows. Eighth, I drop burst shots within thirty seconds of an already-accepted result.

Then the results are serialised to JSON, cross JNI once, and get parsed in JS.

**The key idea is the ordering: the graph is a hard filter, CLIP is a soft ranker.** Filter first because it's exact
and it cuts a hundred thousand candidates to forty; rank second because it's approximate and expensive."

**Follow-up:** *"What if 'mom' resolves to the wrong person?"* → Then every result is wrong and there is no
confidence signal to warn the user. The edit-distance-2 threshold is genuinely too loose on short strings — 'mom' to
'tom' is distance 1. It should scale with length.

---

### Q2. Why is the compute in C++ and the UI in JavaScript? Isn't that an awkward split?

**A.** "It would be awkward if the boundary were chatty. It isn't — I cross it **once per operation**, not once per
item. Indexing fifty thousand photos is *one* JNI call; the loop, all inference, all SQLite writes and all
clustering happen natively. A search is one call in, one JSON string out.

The split follows where the cost is. The UI renders a grid and collects a string — nothing frame-budget sensitive.
The engine does CLIP, SCRFD, ArcFace, SQLite and clustering, which is 10²–10³× more expensive than any bridge hop.
So React Native's known weakness sits on the part of the system that costs nothing, and its strength — iteration
speed — sits on the part where my time actually went.

The forcing function for C++ specifically is more concrete than performance, though: **sqlite-vec is a loadable C
extension and Android's SQLite API cannot load extensions.** To have vector search at all I had to compile my own
SQLite amalgamation into a native library. ONNX Runtime and OpenCV being C++ libraries reinforced it."

**Follow-up:** *"So would Jetpack Compose have been better?"* → Honestly, yes. Given that iOS stayed a stub,
Compose would have removed an entire runtime and language while keeping everything else. I'd defend RN as reasonable
at the time and concede Compose as the better choice in hindsight.

---

### Q3. Your README says search is ~280 ms. Where does that come from?

**A.** "From reading my own logs on my device during development — `search_images` brackets the whole pipeline with
`std::chrono` and logs the elapsed milliseconds. But I want to be straight with you: **there's no benchmark harness
in the repo, no query set, and no recorded results**, so it's an informal observation, not a number I'd defend.

I'd also stop reporting a single average, because the two code paths have completely different cost models. An
entity-restricted search is dominated by the CLIP text encoder — a fixed 30 to 80 milliseconds independent of library
size. A global KNN is a brute-force scan, roughly a millisecond per thousand vectors, so it's **linear in library
size**. Averaging those hides the only interesting fact about them.

If you wanted a number I'd stand behind, I'd need a fixed query set, a named device, and p50/p95 reported per path."

**Follow-up:** *"What's actually the biggest cost in a search?"* → The CLIP text encoder, and I don't cache it, so
typing the same query twice re-runs the whole transformer. An LRU cache would make repeats nearly free. The other
one that grows with library size is `verify_entities_in_file`, because `init_face_schema` creates no indexes at all —
so that query full-scans a table with one row per face in the library.

---

## ML

### Q4. Explain the contrastive objective, and why an image embedding and a text embedding can be compared at all.

**A.** *(see [09](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md))* "Take a batch of N image–caption pairs. Encode all N
images and all N captions, L2-normalise everything, and form the N×N matrix of cosine similarities scaled by a
learned temperature. The N diagonal entries are the true pairs; the N²−N off-diagonal entries are in-batch
negatives. The loss is symmetric cross-entropy — treat each row as an N-way classification over captions, each
column as an N-way classification over images, and average.

**Why the spaces are comparable is the whole point of the objective:** the two encoders are trained *jointly*, and
the only thing the loss can do to lower itself is move matching image and text embeddings toward each other and push
non-matching ones apart. There's no separate alignment step — the shared space *is* what the loss constructs. The
projection matrices at the end of each encoder, `W_image` and `W_text`, are the learned maps into it.

Two consequences I actually rely on. First, normalising both to unit length makes cosine similarity equal to the dot
product, which is why every vector in my database is stored normalised and ranking is a plain dot product. Second,
the space has no absolute calibration — a cosine of 0.31 doesn't mean '31% relevant'. Scores are only comparable
**across candidates for the same query**, which is exactly how I use them, and it's why I can't set a global
relevance threshold."

**Follow-up:** *"Why does batch size matter so much for CLIP?"* → Negatives come from the batch, so a larger batch is
a harder discrimination task and a more informative gradient. CLIP trained at 32,768. That's also precisely what
SigLIP's sigmoid loss fixes — it scores each pair independently, so it decouples from batch size.

---

### Q5. Why can't CLIP recognise your mother?

**A.** *(the full answer is in [09 §Part 4](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md) — the three-part core:)*

"Three reasons, and they're about the objective rather than the capacity.

The **training signal**: CLIP learns from web alt-text, which says 'a man smiling at a party', essentially never
'Aaryan'. There's no gradient that rewards separating one ordinary person from another.

The **objective actively opposes it**: contrastive learning pulls together things that share a caption. Two
different men at two different beaches share 'a man at the beach', so CLIP is explicitly trained to make them
*similar*. Identity discrimination is the opposite of what the loss optimises. ArcFace optimises an angular margin
that maximises separation *between identities* — it's built to do the thing CLIP is built not to do.

The **geometry is wrong**: CLIP compresses a whole 224×224 scene into one vector dominated by location, activity and
composition. A face might be 2% of the pixels. ArcFace embeds an *aligned 112×112 crop of just the face*, so all its
capacity goes to identity with pose and scale already normalised out.

That's the architectural argument for my whole system: CLIP is a scene model, ArcFace is an identity model, and
neither substitutes for the other."

---

### Q6. You cluster faces. What happens when one person's embeddings form multiple modes?

**A.** "My current system handles it badly, and it's the deepest flaw in the project.

A person's embeddings aren't one blob. Pose is the dominant axis — frontal versus profile can be a larger
difference than between two different people. Add age, glasses, facial hair, lighting, expression. You get a
genuinely **multi-modal** distribution.

I represent each person with a single vector. Geometrically, the mean of a bimodal distribution lands *between* the
modes, in a region where **no actual face of that person lives**. That's doubly bad: a new frontal face is close to
the frontal mode but far from my representation, so it fails my threshold and founds a spurious second identity —
the person fragments. And the between-modes region is also where *other people's* embeddings sit, so a stranger can
be closer to my representation than any real face of that person is. **Too far from the right faces and too close to
the wrong ones, simultaneously.**

Mine is worse than a mean, actually. It's a normalised exponential moving average with alpha 0.1, so it's
**order-dependent** — index the same library in a different order and you get a different identity — and it's
recency-biased, since after about thirty assignments the earliest faces contribute under 4%.

The fix is the one Apple published: **canonical exemplars.** Keep a handful of diverse, high-quality embeddings per
person and score a new face as the **maximum** similarity over the set rather than distance to an average. A
multi-modal identity gets represented by its actual modes. For me that's a schema change, not an architecture
change — `person_centroids` becomes `person_exemplars` in the same kind of `vec0` table, and the k=1 lookup becomes
k=5 with a group-by-entity max. Ten exemplars is twenty kilobytes per person."

**Follow-up:** *"How would you choose which exemplars to keep?"* → Quality-gated for admission, diversity-gated for
retention: refuse anything below a quality floor, refuse anything with cosine above ~0.95 to an existing exemplar
because it's redundant, and when the gallery is full evict the member with the worst quality-minus-redundancy score.

---

### Q7. How do you decide that 30 new photos are a new person?

**A.** *(see [11 §Part 5](./11_FACE_CLUSTERING_AND_PERSON_SEGREGATION.md))* "Today, badly: the first face that
matches nothing becomes a new identity, with that single embedding as the entire prototype. If that first face was
unusual — bad angle, harsh light — the identity is anchored in the wrong place and every subsequent *good* face of
that person fails my threshold and founds yet another entity. Then a merge pass at the end tries to sew the
fragments back together at a much looser threshold, which risks over-merging. A fragment-then-over-merge cycle,
with no notion of confidence anywhere.

The policy I'd replace it with has one governing principle: **don't create an identity from one face.** A single
embedding is evidence of a *detection*, not of a *person*.

So: gate on quality first. Try to identify against exemplar galleries with a second-best ratio test, so the system
can decline when two candidates are equally close. If it can't identify confidently, **defer** — put the face in an
unassigned pool rather than forcing a decision. Then periodically cluster the pool with average-linkage
agglomerative clustering and mint a new identity **only** when a cluster has at least three mutually consistent
members with sufficient cohesion. Everything else stays 'unknown', which is an honest terminal state rather than a
failure.

For consolidation, merge only when the linkage between two *galleries* — median, not centroid-to-centroid — exceeds
a threshold that is **stricter** than the assignment threshold, and only when the two identities have never
co-occurred in a photograph. That last constraint is free: **if two entities appear in the same photo, they are
definitely different people**, and I already compute and store exactly that in my co-occurrence table. I just never
use it as a constraint."

**Follow-up:** *"Why is a fixed global threshold insufficient?"* → Four reasons: quality varies per face, so the
right threshold for a 400-pixel portrait isn't right for a 40-pixel profile; distinctiveness varies per identity, so
someone with a sibling in the library needs a stricter threshold than someone without; library size matters, because
with 500 identities the chance some wrong centroid falls inside your threshold grows — a multiple-comparisons
problem; and published evaluations show a single global threshold produces different false-match rates across
demographic groups, so it isn't just suboptimal, it's unfair. And I can calibrate without labels: co-occurring faces
give me free known-impostor pairs, and each identity's members give me its intra-class distribution.

---

## Systems

### Q8. What breaks first as the library grows?

**A.** "Not what people expect. It's not the vector search.

**The first wall is face-crop disk usage**, at around fifty thousand photos. I write a 320×320 aligned JPEG for
every detected face during indexing. At a hundred thousand photos that's roughly four and a half gigabytes —
about eight times my entire database — and it exists to render avatars on the People page, where exactly **one crop
per person** is ever displayed. It's entirely self-inflicted and the fix is to generate them lazily from the
bounding box I already store.

**Second is indexing wall-clock.** Single-threaded at maybe two images per second is fourteen hours for a hundred
thousand photos, before thermal throttling. Three fixes get 3–5×: decode each image once instead of twice, batch
ONNX inference — both graphs already declare dynamic batch dimensions and I use batch size 1 — and pipeline decode,
inference and DB writes. Though I'd be careful about that last one: ORT already saturates the cores during
inference, so pipelining recovers the non-inference time, maybe 1.3–1.6×, not a linear speedup.

**Third is a missing index.** `init_face_schema` creates the tables and **no indexes at all**, so
`verify_entities_in_file` full-scans a table with one row per face, once per search result. Two `CREATE INDEX`
statements.

Brute-force KNN only becomes a problem past a hundred thousand vectors, and even then only on the global-search
path, because any query that resolves an entity scores forty candidates instead of a hundred thousand.

**But none of those is what I'd fix first.** Clustering quality isn't a scaling wall — it's a correctness ceiling
that's bad at a thousand photos and bad at a hundred thousand, just more visibly. A faster system that puts your
aunt in your mother's album isn't better."

---

### Q9. How does the system recover from being killed mid-index?

**A.** "This part I'd actually defend. Progress lives in a **`status` column**, not in memory — the loop is
`SELECT ... WHERE status='pending' LIMIT 10`, and `save_clip_embedding` flips a row to `'indexed'`. So killing the
app loses at most the in-flight photo, and the next run resumes exactly where it stopped with nothing redone. WAL
mode means no partial transaction is ever visible, and it also means a search during indexing never blocks.

Where it's weaker: the per-photo work spans **four separate transactions** plus a couple of autocommitted
statements, so there's no single atomic 'this photo is fully indexed' unit. A crash between them leaves a photo
marked indexed whose faces weren't processed. The fix is one `BEGIN IMMEDIATE` per photo with the status update
last, inside the transaction.

And there are two robustness gaps I'd flag before you find them. There's **no `busy_timeout` set on any connection
in the codebase**, so if the reset-face-data job and the indexer overlap — both reachable from the UI — the loser
gets `SQLITE_BUSY` immediately instead of waiting. That's a one-line fix. And `sqlite3_step`'s return code is
discarded in about fifteen places, which is *why* several bugs in my own audit are invisible: a disk-full or a
constraint violation currently looks exactly like success."

---

### Q10. Where can a race condition occur in your code?

**A.** "Four places, and two of them are real bugs.

**The tokenizer load is a genuine data race.** In `searchImagesNative` I guard CLIP-session and NER creation with
`g_session_mutex`, but the CLIP tokenizer load sits *outside* that scope. Every search from JS spawns a raw Kotlin
`Thread` with no serialisation, so two searches issued before the first finishes loading will both enter that block
and concurrently mutate the same `unordered_map`. Undefined behaviour, and the realistic symptom is a crash on the
first two rapid searches after a cold start.

**Worse, there's a use-after-free.** When the user renames a person, `rebuild_query_engine_dictionary` does
`delete g_analyzer; g_analyzer = nullptr; init_query_engine(...)` — with no lock — while a concurrent search may be
executing inside that object. Fix is a `shared_ptr` with an atomic swap, or holding the mutex across both.

Two milder ones: `g_index_status` and `g_current_processing_file` are `std::string`s written by the indexing thread
and read by the progress call on another thread, which is a torn read or worse across a small-string-to-heap
transition. And `g_owner_entity_id` is a plain `int64_t` — formally a race, though aligned 64-bit loads on aarch64
make tearing implausible.

What's *not* a problem: `Ort::Session::Run` is thread-safe for concurrent calls, so inference needs no lock, and my
progress counters are properly `std::atomic`."

---

### Q11. Explain your transaction and consistency model.

**A.** *(see [18](./18_FAILURE_MODES_AND_CONSISTENCY.md))* "One writer, N readers, WAL. The indexer holds the write
connection from `initialize_database`, which sets `journal_mode=WAL` and `foreign_keys=ON`. Searches open separate
read-only connections. WAL is the load-bearing choice: readers don't block the writer and the writer doesn't block
readers, which is exactly what I need for interactive search during a multi-hour index.

Clustering, reclustering, merging and graph updates are each wrapped in explicit transactions with rollback.
`insert_files` wraps the whole batch in one transaction — that's the difference between one fsync and a hundred
thousand.

**The subtle thing worth raising:** foreign keys enforce my relational integrity, but my three biggest tables —
`clip_vec`, `face_vec`, `person_centroids` — are `vec0` **virtual tables**, and **foreign keys don't apply to
virtual tables**. So `ON DELETE CASCADE` does nothing for them. `merge_similar_entities` actually gets this right —
it explicitly deletes from `person_centroids` rather than relying on a cascade — but nothing does the equivalent for
`clip_vec`. It's latent today because I never delete anything, but the moment I add deletion, every vector table
becomes an orphan source.

And I found the opposite problem by testing: the merge pass **relies** on cascade in a place it shouldn't. Deleting
the merged entity cascade-deletes its `entity_relations` rows instead of transferring them to the keeper, so the
O(1) two-person lookup silently degrades to a set intersection afterwards. I verified that by running the real
schema."

---

### Q12. Is Vanta cross-platform?

**A.** "No, and I'd rather say that plainly. **iOS is a fifteen-line Swift stub** with two hello-world functions —
one of them literally returns 'C++ not implemented on iOS yet'. And Android is **arm64-v8a only**, because the
bundled ONNX Runtime and OpenCV binaries are arm64. That also means it won't run on an x86_64 emulator.

What's true is that the C++ core is **portable by construction**: nothing in the query engine, clustering, graph or
retrieval code touches JNI — that's confined to one adapter file. The dependencies (ORT, OpenCV, SQLite) all build
for iOS. So a port is writing a new adapter, not a new engine.

But I'd flag the hard part, which isn't the engine: on iOS, `PHPhotoLibrary` gives you `PHAsset`s, not file paths.
My engine currently takes a filesystem path and calls `cv::imread` on it. So the port forces the bytes-or-file-
descriptor-over-the-boundary redesign that I should already be doing on Android anyway for scoped-storage
correctness. The accurate description is **'portable by construction, single-platform in practice'**."

---

## Judgement

### Q13. What's the worst decision you made in this project?

**A.** "Representing each person with a single vector — and compounding it by making that vector an exponential
moving average instead of even a proper mean.

It's the worst because of its **blast radius**. Every other bug I have is local: the aspect-ratio squash degrades
embeddings uniformly, the missing colour swap costs some detection recall, a missing index costs latency. But the
identity representation determines whether the product's headline feature is correct. When it's wrong, the user sees
their mother split across four albums, or their aunt inside their mother's album — and the second one is essentially
unrecoverable, because I have no split UI and no provenance log of what got merged.

It's also the decision I had the most information available about and used the least. Apple published the
alternative. The fix is a schema change I could do in a week. I chose the simplest thing that worked in a demo and
didn't revisit it."

**Follow-up:** *"So why didn't you fix it?"* → Three weeks, solo, and I was optimising for end-to-end completeness —
getting the whole pipeline working was the right first goal. The honest failure isn't the initial choice, it's that
I never built the evaluation harness that would have shown me how badly it was doing.

---

### Q14. What would you tell someone starting this project today?

**A.** "Three things.

**Build the evaluation harness before the third feature, not after the last one.** I have three user-facing search
toggles — graph, spellcheck, intent — and I cannot tell you whether any of them improves results. I suspect one of
them actively hurts, because the intent builder silently drops words it doesn't recognise. That's not a missing
number; it's an inability to make decisions.

**Write golden tests against the reference implementation for every preprocessing path.** Two of my three real
accuracy bugs are preprocessing mismatches — an anisotropic resize where CLIP expects resize-and-crop, and a missing
BGR-to-RGB swap where InsightFace uses `swapRB=True`. Both are invisible from the UI, both degrade everything
silently, and both would have been caught in an afternoon by embedding twenty fixture images in Python and asserting
cosine above 0.99 against the C++ path.

**And check your return codes.** I discard `sqlite3_step`'s result in about fifteen places. That single habit is why
a column that's never written, a foreign-key rejection, and a cascade that deletes graph edges all went unnoticed —
the database was telling me and I wasn't listening."
