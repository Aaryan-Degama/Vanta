# 20 — Testing & Evaluation

## Part 1 — What exists today [VERIFIED-REPO]

| Test file | What it covers |
|---|---|
| `__tests__/App.test.tsx` | Renders the root navigator with mocked natives — a smoke test |
| `__tests__/SearchScreen.test.tsx` | Renders SearchScreen, asserts the `<Query>` placeholder exists |
| `__tests__/ThemeContext.test.tsx` | Asserts the light-mode background colour |
| `__tests__/formatting.test.ts` | 8 real assertions on `formatBytes` / `formatDate` |

**Four test files. Roughly 12 assertions. All JavaScript.**

`jest.config.js` sets `collectCoverageFrom: ['App.tsx', 'ThemeContext.tsx', 'screens/**', 'modules/vanta-bridge/src/**']`
— i.e. coverage is only measured over the **TypeScript UI**, which is ~2,300 of the project's ~8,000 lines.

### What has zero automated tests

- **All ~4,500 lines of C++** — the query engine, clustering, graph, SQL, preprocessing, SCRFD decoding, BPE,
  WordPiece, Damerau-Levenshtein, Intent Builder, typo correction.
- **All ~1,200 lines of Kotlin** — MediaStore scanning, classification, the service.
- **The entire JNI boundary.**
- **Every ML component.** No accuracy test, no regression test, no golden output.
- **Retrieval quality.** No Precision@K, Recall@K, MRR or NDCG anywhere.
- **Face quality.** No FAR/FRR, no ROC, no cluster purity.
- **Performance.** No benchmark, no baseline, no regression detection.

### CI

`vanta_app/.github/workflows/ci.yml` is well written — format check, lint, typecheck, Jest, and a native Gradle
build. **It has never run**, for two independent reasons:

1. GitHub Actions only reads workflows from **`.github/workflows/` at the repository root**. This one is at
   `vanta_app/.github/workflows/`. The repo root has no `.github` directory.
2. Even relocated, the `android-build` job does `cd android && ./gradlew :vanta-bridge:assembleDebug`, but
   `vanta_app/android` is **gitignored** (it's Expo prebuild output) and the workflow never runs `expo prebuild`.
   It would also need the Android SDK, the NDK, and the **OpenCV Android SDK**, which is likewise gitignored.

> **How to present this:** *"There's a CI workflow, and I'd rather tell you it's not wired up than let you find out.
> It's in the wrong directory for Actions to pick up, and the native job depends on two directories that are
> gitignored because they're generated. It's a written intention, not a running pipeline."*

### The honest summary

> "My testing is the weakest part of the project and I'd rather lead with that than be caught by it. I have four
> React smoke tests and nothing else. **Every algorithmically interesting thing I wrote — the SCRFD anchor decoder,
> the clustering thresholds, the BPE tokenizer, the graph intersection — is untested.** That's not an oversight I'd
> defend; it's a consequence of building end-to-end in three weeks and it's the first thing I'd fix. And it has a
> concrete cost I can point at: three of the bugs in my own audit — a column that's never written, a foreign-key
> rejection that's silently swallowed, and a cascade that deletes graph edges — would all have been caught by a
> single integration test that indexed ten fixture photos and asserted on the resulting database."

---

## Part 2 — The test suite Vanta should have

### Unit — C++ (GoogleTest, compiled for the host, no Android needed)

| Target | Tests |
|---|---|
| `damerau_levenshtein` | identity=0, single insert/delete/substitute=1, **transposition=1** (the whole point), empty strings, case-insensitivity |
| `CLIPTokenizer::encode` | **Golden tests against Python `clip.tokenize()`** for 50 strings — this is the highest-value test in the list, because a tokenizer mismatch silently degrades every query |
| `BertTokenizer` | Golden tests against HuggingFace `BertTokenizerFast`; `##` continuation; `[UNK]`; truncation preserving `[SEP]`; `word_ids` alignment |
| `NERModel::decode_spans` | `B-I-I` → one span; `B-B` → two spans; bare `I-X` → new span; trailing span flushed; `O` closes |
| `Face_embedding::nms` | Two identical boxes → 1 survivor; IoU just above/below 0.4; empty input |
| SCRFD decode | **Feed a synthetic output tensor with one known anchor above threshold and assert the exact decoded bbox and 5 keypoints.** This would have caught a stride or index error instantly |
| `IntentBuilder::buildIntent` | 🔴 **`"my dog at the beach"` must not lose `dog`.** Write this test and watch it fail — see [01 §C1](./01_VANTA_REALITY_CHECK.md) |
| `VantaProductionAnalyzer` | Whitelisted words untouched; digit protection (`mp4`); dist-1 correction accepted; dist-3 rejected |
| `l2Normalize` | Unit norm after; zero vector doesn't produce NaN |
| Letterbox round-trip | A box in 640-space maps back to the correct original coordinates for portrait, landscape and square inputs |

### Integration — database (in-memory SQLite + sqlite-vec, no models)

| Test | Asserts |
|---|---|
| Schema creation is idempotent | Run `initialize_database` twice; no error; migrations don't duplicate columns |
| **`cluster_faces_for_file` with synthetic embeddings** | Two near-identical vectors → **one** entity. Two orthogonal vectors → **two** entities. This is where you'd inject known vectors and test the threshold behaviour directly |
| 🔴 **`entity_memberships.score`** | Insert a face, assert `score != 1.0`. **Fails today** ([01 §A1](./01_VANTA_REALITY_CHECK.md)) |
| 🔴 **`recluster_pending_faces` tombstone** | Assert an unmatchable face ends with a terminal state. **Fails today** (FK rejects `-1`) |
| 🔴 **`merge_similar_entities` preserves edges** | Create A–B co-occurrence, merge B into C, assert an A–C edge exists. **Fails today** |
| `update_graph_for_file` | 3 people in a photo → exactly 3 edges, canonical ordering, `co_occurrence_count` increments on the second photo |
| Multi-entity intersection | Files containing {1,2,3} correctly intersected; smallest-set optimisation picks the right set |
| Crash consistency | Kill mid-transaction (simulate), reopen, assert no partial state |

### Integration — JNI

Run under Android instrumentation (`androidTest`) or Robolectric:
- Round-trip a `FileMeta[]` and assert every field survives (**this would catch `display_name` being dropped**).
- Assert **no local-reference leak** with a 10,000-element array.
- Assert `searchImagesNative` returns valid JSON for a query containing `"` and `\`.
- Assert a C++ exception doesn't kill the process.

### ML — model behaviour (golden/characterisation tests)

| Test | Why |
|---|---|
| **CLIP embedding golden vectors** | Embed 20 fixture images in Python with the reference `preprocess`; embed the same images through the C++ path; assert cosine > 0.99. 🔴 **This test would fail today** because of the aspect-ratio squash ([09](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md)) — which is exactly why it's worth writing |
| **ArcFace golden vectors** | Same, against InsightFace's Python pipeline. Would confirm alignment and normalisation are right |
| **SCRFD golden detections** | Same fixture images through Python InsightFace vs C++. 🔴 **Would surface the missing BGR→RGB swap** ([10](./10_FACE_RECOGNITION.md)) |
| **INT8 vs FP32 delta** | Embed the same faces with both; report the cosine distribution. Quantifies the quantisation cost that is currently **[UNKNOWN]** |
| **NER held-out F1** | Per-label precision/recall/F1 on a held-out query set |

> These golden tests are the highest-leverage thing on this whole page: **two of the three preprocessing bugs in this
> audit are exactly what a golden test is designed to catch**, and neither is visible from the UI.

### Retrieval evaluation

**Metrics, defined:**

| Metric | Definition | What it tells you here |
|---|---|---|
| **Precision@K** | (relevant in top K) / K | How much junk is on screen |
| **Recall@K** | (relevant in top K) / (total relevant) | Whether the photo you wanted is findable at all |
| **MRR** | mean of 1/rank of the first relevant result | Good for "find *the* photo" queries |
| **NDCG@K** | $\frac{DCG@K}{IDCG@K}$, $DCG@K=\sum_{i=1}^{K}\frac{2^{rel_i}-1}{\log_2(i+1)}$ | The right metric for **graded** relevance and position-weighted ranking |

**The protocol** — and the answer to "how would you evaluate a private, unlabelable system":

> "I can't collect user data, so I'd build a **synthetic personal library**: a few thousand public-domain /
> CC-licensed photos assembled into a plausible library with a known cast of people (repeat individuals across
> scenes, from a consented or public face dataset), known events and known scenes. Because I construct it, I have
> perfect ground truth for free. Then ~200 realistic queries across four families — pure semantic ('a dog on grass'),
> pure identity ('photos of mom'), conjunctive ('me and mom'), and compositional ('me and mom at the beach') —
> and report Recall@10 and NDCG@10 **per family**, because those four paths exercise completely different code and
> averaging them hides everything.
>
> That gives me the thing I most lack: **an A/B harness.** Right now I have three user-facing toggles —
> `useGraph`, `useSpellCheck`, `useIntent` — and I genuinely cannot tell you whether any of them helps. With this
> harness, each becomes a measurable ablation, and I'd start with the Intent Builder, because I suspect it *hurts*
> on queries where it partially matches."

### Face evaluation

| Metric | Definition |
|---|---|
| **FAR / FMR** | Fraction of impostor pairs accepted. "Not-mom returned as mom" |
| **FRR / FNMR** | Fraction of genuine pairs rejected. "Mom's photo not found" |
| **ROC / DET curve** | TAR vs FAR across thresholds; report **TAR@FAR=1e-3, 1e-4** |
| **EER** | Where FAR = FRR. A single summary number |
| **Cluster purity** | $\frac{1}{N}\sum_c \max_j |c \cap g_j|$ — fraction correctly assigned if each cluster takes its majority identity |
| **Pairwise precision/recall + F1 (BCubed)** | Over all same-cluster pairs: are they truly the same person? **The right primary metric for Vanta**, because it penalises both over- and under-merging |
| **Identity consistency** | Across a re-index, how stable are cluster assignments? 🔴 Vanta's EMA centroid is **order-dependent**, so this would likely score badly and that's worth knowing |

**The threshold-selection experiment Vanta most needs:** sweep `GOOD_THRESHOLD` from 0.4 to 1.0 on a labelled
library and plot pairwise precision/recall. **The hardcoded 0.6 was chosen by trying a few values on one person's
photos** — this would either validate it or replace it with something defensible.

### Stress, benchmark, regression

| Suite | Content |
|---|---|
| **Stress** | 100k synthetic file rows; 10k faces in one entity; a 200-face group photo; 1,000 entities; concurrent search during indexing; kill -9 mid-index and verify recovery |
| **Benchmark** | Fixed corpus, fixed device, p50/p95 per stage, machine-readable output |
| **Regression** | Fail CI if p95 search latency or indexing throughput regresses >10%; fail if Recall@10 drops >2 points |

---

## Part 3 — The 5 tests to write first

If asked *"you have one day — what do you test?"*:

1. **CLIP golden-vector test** — catches the aspect-ratio bug and locks preprocessing forever. Affects 100% of the index.
2. **SCRFD golden-detection test** — catches the BGR/RGB bug and any future anchor-decoding regression.
3. **Clustering integration test with synthetic embeddings** — makes the thresholds testable and immediately exposes the `score`, tombstone and merge-cascade bugs.
4. **Intent Builder word-preservation test** — three lines, catches a live retrieval-destroying bug.
5. **A 10-photo end-to-end fixture test** — index 10 known photos, assert the full resulting database state. This is the single test that would have caught the largest number of the findings in [01](./01_VANTA_REALITY_CHECK.md).

Note what's *not* on that list: unit tests for `formatBytes`. Vanta already has those. The tests it has are the
tests that were easiest to write, not the ones that would have found its bugs — and being able to say that about
your own project is a strong signal.
