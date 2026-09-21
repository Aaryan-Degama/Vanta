# 24 — 120 Rapid-Fire Questions

> **Q** · **A** (2–5 sentences) · **↳** likely follow-up.
> Repository-grounded. Where a number is not verifiable it says so — do the same in the room.

---

## Architecture (1–12)

**1. What is Vanta in one sentence?**
An Android app that indexes your local photos with on-device ML and lets you search them in natural language,
including by person, with no network access at all.
**↳** *Why is that hard?* Open vocabulary forces embeddings; identity forces a second model and a second index.

**2. What are the layers?**
React Native/TypeScript UI → Expo native module in Kotlin → JNI → a ~4,500-line C++ engine → SQLite + sqlite-vec.
**↳** *Where's the compute?* Entirely in C++; the bridge is crossed once per operation.

**3. Why two indexes instead of one?**
CLIP vectors answer "what does this look like"; the entity graph answers "who is in this, together". A dot product
can't enforce a conjunctive set constraint, and no embedding represents "not".
**↳** *Give an example only the graph can do.* "Photos of me without my brother" — that's a `NOT EXISTS`.

**4. What's the single most important architectural idea?**
Identity is a **filter**, semantics is a **ranker**. Filter first because it's exact and cheap; rank second because
it's approximate and expensive.

**5. How many JNI entry points are there?**
20, in `vanta.cpp`. Eleven of them return a hand-built JSON string.

**6. Why JSON over the bridge instead of structured objects?**
One `NewStringUTF` instead of building N `HashMap`s with `FindClass`/`GetMethodID`/`CallObjectMethod` per field.
**↳** *Cost?* The contract is untyped and has already drifted: `SearchScreen.tsx` expects `size`/`date_taken`; C++
emits `size_bytes`/`mtime_unix`.

**7. What runs on which thread?**
JS thread renders; an Expo worker dispatches; each search spawns a raw Kotlin `Thread`; indexing runs on one
dedicated thread inside a foreground `Service`; ORT fans out internally.

**8. What's the process model?**
Single process. A native crash takes down the whole app — `ErrorBoundary.tsx` only catches React render errors.

**9. Where does state live?**
`vanta.db` in the app's databases dir; models and face crops in `filesDir`; owner entity in `SharedPreferences` and
a C++ global.

**10. How do you know indexing progress?**
JS polls `getIndexProgressNative()`, which returns `{processed, total, status, currentFile}` as JSON.
**↳** *Better design?* Expo `EventEmitter` push, so you're not waking the JS thread on a timer for hours.

**11. What's the biggest structural weakness?**
Identity representation — one EMA vector per person. See Q57.

**12. Is any of it cross-platform today?**
No. iOS is a 15-line stub; Android is arm64-v8a only. The C++ core is portable in principle; only the adapter is
platform-specific.

---

## React Native / Expo (13–22)

**13. Why React Native?**
~0% of the compute is in the UI, so RN's overhead lands where it doesn't matter, and its iteration speed lands where
my time actually went.
**↳** *Better choice in hindsight?* Jetpack Compose — since iOS stayed a stub, it would have removed a whole runtime.

**14. Why Expo, not plain RN?**
The Expo Modules API replaces `ReactContextBaseJavaModule` + `@ReactMethod` + manual `ReadableMap` conversion with
typed `AsyncFunction` declarations, plus autolinking and prebuild.

**15. Managed or bare workflow?**
Prebuild/bare. Managed is impossible — Vanta ships a custom `.so`, needs the NDK and CMake, and runs a foreground
service. Expo Go can't load any of it.

**16. What can't Expo do here?**
Bundle a 27 MB ONNX Runtime, drive CMake, express MediaStore queries, or marshal `FileMeta` into C++.

**17. Why not Flutter?**
It wouldn't remove any work. All the platform-specific code is Android-specific, so I'd still write the identical
Kotlin layer and add Dart on top.

**18. Why TypeScript?**
Type safety across 6 screens and 20 native calls — though the boundary itself is untyped JSON strings, which is
where the real drift happened.

**19. What's `requireNativeModule`?**
Expo's runtime resolver that binds the TS declaration to the platform implementation — Kotlin on Android, the Swift
stub on iOS, a web stub in the browser.

**20. Why is there a web stub at all?**
So `expo start --web` renders the UI shell for development. It returns "VantaEngine is not supported on web".

**21. Any dead code in the JS API?**
Yes — `helloFromKotlin()` and `helloFromCpp()` are exported from `index.ts` and declared in TS, but **not
implemented in the Kotlin module**. They'd throw.

**22. How is the UI tested?**
Four Jest files, ~12 assertions, all TS. Nothing tests C++, Kotlin, JNI, SQL or any model.

---

## Kotlin / Android (23–35)

**23. Why Kotlin over Java?**
Null safety across JNI (`absPath: String?` forces the C++ side to handle null, and it does), data classes,
`Sequence`, `use{}`, and it's the Android and Expo default. **Not for speed** — both compile to the same bytecode.

**24. How do you discover media?**
`MediaStore` cursors over Images, Video, Audio and the generic Files collection, yielded lazily as a `Sequence` in
batches of 200.

**25. How do you classify file types?**
Extension first, MIME second. Providers return wrong or generic MIME types often enough that the filename is more
trustworthy when it exists.

**26. What's scoped storage and how does it bite you?**
From Android 10/11, `MediaColumns.DATA` (the absolute path) is deprecated and may be null. My Kotlin layer documents
that `absPath` must not be used to open files — **and my C++ layer uses it anyway**, because OpenCV wants a path.
**↳** *Fix?* `MediaLoader.readBytes(context, uri)` already exists in the repo and is never called. Better still,
pass a file descriptor from `openFileDescriptor` — zero-copy and scoped-storage-correct.

**27. What permissions are declared?**
`READ_MEDIA_IMAGES/VIDEO/AUDIO`, `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_DATA_SYNC`, and the service with
`foregroundServiceType="dataSync"`, `exported="false"`.
**↳** *Gap?* No `READ_EXTERNAL_STORAGE`. `READ_MEDIA_*` is API 33+, but minSdk is 24 — so on Android 12 and below
there's no permission at all and the scan silently returns nothing.

**28. Why a foreground service?**
Android freezes backgrounded processes; indexing takes hours. A foreground service with a notification gets a much
higher OOM-adjustment score.

**29. Why not WorkManager?**
It's the better long-term answer — reboot survival, Doze awareness, charging constraints, backoff. But it chunks
work, and my indexer is one long blocking JNI call.

**30. Does indexing auto-resume after a kill?**
Work isn't lost (progress is the `status` column), but it doesn't restart automatically: `START_STICKY` redelivers a
**null Intent**, so the `ACTION_START` check fails.

**31. How do models get onto disk?**
`extractAssetsIfNeeded` copies them from APK assets to `filesDir` once, because ORT wants a regular file to mmap.
**↳** *Cost?* The models exist twice — compressed in the APK and expanded in `filesDir`.

**32. Any bug in asset extraction?**
Two divergent copies of the file list: `VantaEngineModule` copies 9 files, `IndexingService` copies 6 (missing the
NER files). Module `OnCreate` runs first, so it works by ordering.

**33. Is the scan idempotent?**
Yes — `INSERT OR IGNORE` with `abs_path UNIQUE`. Rescanning after adding 50 photos inserts 50 rows.

**34. What happens to deleted photos?**
Nothing. The row, the CLIP vector, the face detections and the graph edges persist forever, and search returns a
broken image.

**35. Modified photos?**
Also nothing. `mtime_unix` is stored but never compared, and `INSERT OR IGNORE` discards the update.

---

## JNI (36–47)

**36. What's `JNIEnv` and what's the rule?**
A per-thread table of JNI function pointers. **Thread-local — never cache it across threads.**

**37. What's `JavaVM` for?**
Process-wide handle for `AttachCurrentThread`. Vanta never uses it, because it never calls back into Java.

**38. Does Vanta define `JNI_OnLoad`?**
No. It relies on name-mangled discovery (`Java_pkg_Class_method`), which requires `extern "C"`.
**↳** *Alternative?* `RegisterNatives` — faster resolution, arbitrary C++ names, and it fails at library-load time
rather than first call.

**39. How many JNI crossings to index 50,000 photos?**
**Three**, plus one per progress poll. Not 50,000.

**40. Why is per-image JNI bad?**
Not the transition cost — that's tens of nanoseconds. You couldn't hold a transaction open, batch inserts, or keep
warm native state across calls.

**41. Where is Vanta's JNI design actually weak?**
`startStoring` passes the entire scan in one array. At 100k files that's **[ESTIMATED]** ~25 MB of JVM heap and
~1.2 M accessor calls in one crossing — and it throws away the 200-file batching `scanPhone` already provides.

**42. What's a local reference and why does it matter?**
Every `jobject` JNI returns occupies a slot in a small table (~512 on ART). Vanta correctly `DeleteLocalRef`s every
string and object in the loop — without that, 100k files would abort the VM with a local reference table overflow.

**43. How do you read a Kotlin enum over JNI?**
`GetMethodID(cls, "name", "()Ljava/lang/String;")` then `CallObjectMethod`. Kotlin enums are Java enums.

**44. Nullable `Long?`?**
It arrives as a boxed `java.lang.Long` object. Null-check it, then `CallLongMethod(obj, longValue)`.

**45. What's missing from the JNI error handling?**
Every exception check. No `ExceptionCheck`/`ExceptionClear` anywhere, and calling further JNI functions with a
pending exception is undefined.

**46. What if a C++ exception escapes into the JVM?**
`std::terminate` — the process dies. Coverage in Vanta is incidental: the per-image body and the text-embedding call
are wrapped, but by accident more than design.

**47. Zero-copy options?**
`GetPrimitiveArrayCritical` on primitive arrays, or `ByteBuffer.allocateDirect` + `GetDirectBufferAddress`. For
image bytes, best is passing a detached file descriptor and `mmap`ing it natively.

---

## ONNX Runtime (48–56)

**48. What version and what EP?**
ONNX Runtime **1.26.0**, prebuilt aarch64, **CPU only**. No `AppendExecutionProvider` call exists anywhere.

**49. Why ONNX Runtime over TFLite?**
All three models are natively ONNX — InsightFace ships `.onnx` directly and CLIP exports in 20 lines. TFLite would
mean owning a PyTorch→ONNX→TF→TFLite conversion for all three.

**50. Why CPU only?**
Determinism. NNAPI behaviour varies by vendor and ops silently fall back — and in my system a face embedding's exact
value decides whether two people get merged, so cross-device non-determinism is a correctness hazard.
**↳** *What would you try first?* XNNPACK — still CPU, still deterministic, and it's ORT's optimised path for
quantised convnets.

**51. How many `Ort::Env` objects does Vanta create?**
**Three** — one per model class. ORT documents one per process; each carries its own logging sink and arena.

**52. Thread configuration?**
CLIP and face sessions use default-constructed `SessionOptions` (so ORT uses all cores). Only NER sets threads, to 1.

**53. Why 1 thread for NER?**
BERT-tiny at sequence length 32 is ~10 M FLOPs. Dispatch and sync overhead would exceed the compute.

**54. Is `Session::Run` thread-safe?**
Yes, for concurrent calls on the same session. That's why Vanta needs no lock around inference — only around session
*creation*.

**55. What's the `Ort::Value` memory contract?**
`CreateTensor` wraps the caller's buffer **without copying**, so the backing vector must outlive `Run()`. Outputs
are owned by ORT and valid only while the returned `Ort::Value`s live.

**56. What batch size does Vanta use?**
1, always — even though both CLIP and ArcFace graphs declare **dynamic batch dimensions**. Batching 4–8 is
**[ESTIMATED]** 1.5–2.5× indexing throughput, unclaimed.

---

## Precision & quantisation (57–63)

**57. What precision is each model?**
CLIP image/text: **FP16**. SCRFD and ArcFace: **INT8 dynamic (QUInt8)** — verified from the ONNX graphs
(`ConvInteger`, `MatMulInteger`, `DynamicQuantizeLinear`). NER: **FP32**.

**58. Why the split?**
Convnets quantise to INT8 well and `quantize_dynamic` needs no calibration data — which I can't collect, by design.
CLIP is a ViT, where INT8 is delicate (attention softmax, LayerNorm), so FP16 is the conservative middle.

**59. Why doesn't FP16 mean 2× faster?**
It halves *bandwidth*, not necessarily arithmetic; the CPU may lack ARMv8.2 FP16 vector arithmetic and convert to
FP32 internally; `Cast` nodes appear at mixed-precision boundaries; and Vanta pays an explicit **150,528 scalar
FP32→FP16 conversions per image** on the critical path.

**60. Dynamic vs static quantisation?**
Dynamic computes activation scales at runtime — no calibration data needed. Static calibrates offline on
representative data: faster and more accurate, but requires data.

**61. What's the accuracy cost of your INT8 face models?**
**[UNKNOWN]** — I never measured it. It's the first evaluation I'd add, because INT8 face embeddings are exactly
what would silently degrade the weakest part of my system.

**62. How big is each model on disk?**
CLIP image ~176 MB, CLIP text ~127 MB **[ESTIMATED from parameter counts]**; `det_10g` 4.11 MB; `w600k_r50`
41.76 MB; NER 16.94 MB — those last three **[VERIFIED-REPO]**.

**63. Why is disk size ≠ RAM?**
mmap'd weights are file-backed and reclaimable; INT8 weights dequantise into float activations; activations are
substantial (SCRFD's stride-8 map is 80×80×C); and ORT's arena allocator holds the high-water mark for the session's
lifetime.

---

## CLIP (64–76)

**64. Which CLIP?**
OpenAI **ViT-B/32**, exported at opset 18 from `model.visual` and a wrapper around `encode_text`.

**65. Why does exporting `model.visual` work?**
Because OpenAI's `VisionTransformer.forward` applies `x @ self.proj` internally, so it already outputs the **512-d
shared space**. If the projection were outside, the two encoders would live in different spaces.

**66. How many patches?**
224/32 = 7 per side → **49 patches** + a `[CLS]` token = 50 tokens. 12 layers, width 768.

**67. What's the text side?**
BPE to 77 tokens between 49406/49407, 12 layers, width 512, causal; the `[EOT]` representation is projected to 512.

**68. State the contrastive loss.**
N×N cosine matrix scaled by a learned temperature; diagonal = positives, off-diagonal = in-batch negatives;
symmetric cross-entropy over rows and columns.

**69. Why L2-normalise?**
So cosine = dot product. It also gives the identity $\cos = 1 - L2^2/2$, which is how every sqlite-vec L2 threshold
in my code converts to a cosine.

**70. What's the CLIP preprocessing bug?**
`cv::resize(img, 224×224)` is an **anisotropic squash**. Reference CLIP resizes the shortest side to 224 then
centre-crops. A 16:9 photo is stretched 1.78× horizontally — a train/inference mismatch on **100% of the index**.
**↳** *Why not just centre-crop then?* It discards 44% of a 16:9 frame, and subjects aren't always centred. I'd test
padding-to-square against reference-crop — but only after building an eval harness.

**71. Any other CLIP-path bug?**
`get_text_embedding` guards `if (norm > 0)` before dividing; `get_embedding` (image) doesn't. A degenerate embedding
would write 512 NaNs into `clip_vec` and poison every KNN.

**72. What can't CLIP do?**
Identity, counting, spatial relations, compositional attribute binding, negation, small objects, reliable OCR.

**73. Why didn't you use SigLIP?**
Better model — the sigmoid pairwise loss decouples from batch size — but the strong checkpoints are ViT-B/16, which
is 196 patch tokens vs my 49, roughly 4× the attention cost, on a pipeline that already takes hours.

**74. Why not MobileCLIP?**
No good reason, and it's what I'd use today: ~11 M-param image encoder at roughly ViT-B/16 retrieval quality. Would
cut indexing cost several-fold and shrink the APK by >150 MB.

**75. Cheapest model upgrade available right now?**
**OpenCLIP's LAION-2B ViT-B/32 weights** — identical architecture, identical export, identical latency, better
retrieval. Free accuracy.

**76. What's your own tokenizer missing?**
Byte-level unicode mapping, so non-ASCII silently loses tokens; and unknown subwords are dropped rather than mapped
to UNK.

---

## Face recognition (77–92)

**77. Which face models exactly?**
InsightFace **buffalo_l**: `det_10g.onnx` (SCRFD-10GF, 4,226,183 params) and `w600k_r50.onnx` (ResNet50/ArcFace,
43,591,296 params, 112×112 → 512-d). Both INT8.

**78. Is buffalo_l an architecture?**
No — it's a **model pack**. It contains detection, recognition, genderage and two landmark models. Vanta uses two
of the five.

**79. What does the naming mean?**
`w600k` = WebFace600K training set; `r50` = ResNet50; `mbf` = MobileFaceNet; `det_10g` ≈ 10 GFLOPs at 640×640;
`det_500m` ≈ 500 MFLOPs.

**80. Why buffalo_l over buffalo_sc?**
Recall on small and non-frontal faces — and a missed detection is unrecoverable, since that photo can never be found
by person. Plus stronger embeddings, which matter because my clustering is unsupervised with no human in the loop.

**81. Explain the SCRFD decoder you wrote.**
Strides 8/16/32, **2 anchors per location** sharing a centre, anchor counts 12800/3200/800 at 640×640. Box
regression is **distance-based** (FCOS-style: distances to the four edges, in stride units), keypoints are offsets
from the same centre, then NMS at IoU 0.4 and the letterbox scale is inverted.

**82. What's wrong with the detection preprocessing?**
**Missing `BGR→RGB`.** InsightFace uses `swapRB=True`; `cv::imdecode` gives BGR; the packing loop indexes BGR. The
function's own comment claims the swap that the code doesn't do — and the *recognition* path does convert correctly,
so the two halves of my pipeline disagree about colour convention.

**83. Why does alignment use `estimateAffinePartial2D` and not `estimateAffine2D`?**
Partial fits a 4-DoF **similarity** transform — rotation, uniform scale, translation. The full affine adds shear and
anisotropic scale, which would distort facial *geometry* — and geometry is the identity signal.

**84. What are the target landmarks?**
The canonical ArcFace 112×112 five-point template: `(38.2946, 51.6963)`, `(73.5318, 51.5014)`, `(56.0252, 71.7366)`,
`(41.5493, 92.3655)`, `(70.7299, 92.2041)`.

**85. What can go wrong in alignment?**
`estimateAffinePartial2D` returns an **empty matrix** on RANSAC failure, and `warpAffine` then throws. The avatar
crop is in a try/catch; the 112-px embedding call isn't, so one bad face fails the whole file — losing its CLIP
indexing too.

**86. Explain ArcFace.**
Normalise the weights and features so the logit is $\cos\theta$, rescale by $s$, and add an **additive angular
margin** $m$ to the target class's angle: $\cos(\theta_y + m)$. Since cosine is decreasing, that lowers the target
logit, forcing the network to push $\theta_y$ even smaller — producing intra-class compactness and inter-class
separation.

**87. Why additive *angular* rather than CosFace's cosine margin?**
The margin is geodesically **uniform** on the hypersphere — it means the same thing everywhere. CosFace's
$\cos\theta - m$ gives a non-uniform angular margin; SphereFace's multiplicative $\cos(m\theta)$ is unstable and
needs annealing.

**88. Why does that matter for you specifically?**
Because it makes cosine similarity meaningful **for identities the model never saw**. That open-set property is what
lets me cluster unknown people, which is the whole product.

**89. Classification vs verification vs identification vs clustering?**
Closed-set classification needs a fixed identity list and retraining — impossible here. Verification is 1:1
thresholding. Identification is 1:N against a gallery with a reject option. Clustering partitions unlabelled
embeddings — that's what Vanta does.

**90. Which margin loss would you choose today?**
**MagFace or AdaFace** — not because they beat ArcFace on clean benchmarks, but because the pre-normalisation
feature norm *is* a quality signal, and my biggest gap is having no quality estimate at all.

**91. Is there any face quality filtering?**
**None.** Every detection above `det_score > 0.5` is embedded and clustered, including 14-pixel blurry profiles.

**92. Isn't `det_score` a quality proxy?**
It's the wrong signal. It measures "is this a face", not "is this face usable for recognition". A crisp 20-pixel
face scores high; a slightly soft 400-pixel portrait may score lower.

---

## Clustering (93–104)

**93. Describe the clustering algorithm.**
Online nearest-centroid: `person_centroids MATCH k=1`; assign if within threshold and update the representation;
otherwise create a new entity. Then a relaxed second pass for unmatched faces, then a greedy pairwise merge.

**94. What are the thresholds, in cosine?**
sqlite-vec's default metric is **L2**, so converting: assign 0.60 L2 = **cos 0.82**; blurry 0.75 L2 = **cos 0.72**;
merge 1.0 L2 = **cos 0.50**.

**95. What's wrong with those numbers?**
**Merging is looser than assigning.** A single face needs cos 0.82 to *join* a person, but two entire identities
merge at cos 0.50 — and merging is the irreversible operation. They can't both be right.

**96. Is the centroid a centroid?**
No — it's a normalised **EMA**: `0.9·old + 0.1·new`, re-normalised. `Reads/04` documents a running mean and calls
EMA the "alternative"; the code implements the alternative.

**97. Why is EMA problematic here?**
Order-dependent (re-index in a different order, get a different identity), recency-biased (after ~30 assignments the
earliest faces contribute under 4%), and `sample_count` never enters the update, so a 500-photo identity adapts as
fast as a 3-photo one.

**98. Biggest clustering bug besides the representation?**
**Two faces in the same photo can be assigned to the same person.** Each face matches independently. A person appears
at most once per photo — a free, always-true constraint, ignored.
**↳** *Why does that matter especially?* Siblings and parents look alike *and* co-occur constantly, so the failure
concentrates on exactly the people the product must distinguish. It also corrupts the graph, because
`SELECT DISTINCT` then collapses two people into one node.

**99. Fix for that?**
Per-photo **Hungarian assignment** with a "no entity twice" constraint. For ≤10 faces it's microseconds.

**100. What's the free negative evidence you're not using?**
Co-occurrence. Two entities in the same photograph are **definitely different people** — and I already store that in
`entity_relations`. Feeding it back as a cannot-link constraint is nearly free.

**101. Is a new identity founded from one face?**
Yes — the first embedding *is* the prototype. If it's an odd angle, every subsequent good face fails the threshold
and founds another entity. That's the main fragmentation mechanism.

**102. Any ratio test?**
No — `k=1` only. Classic open-set practice is to accept only if the best match is decisively better than the
second-best. `k=3` costs nothing at my table sizes and would let the system decline to decide.

**103. What does `recluster_pending_faces` do, and does it work?**
It re-matches unassigned faces against the complete centroid set at the relaxed threshold. It correctly declines to
update centroids with poor-quality faces. But its "permanently ignore" tombstone sets `entity_id = -1`, which a
**foreign key rejects** — verified by execution — so those faces stay NULL and get re-processed forever.

**104. Cost asymmetry of clustering errors?**
A false **split** is mildly annoying and trivially user-fixable. A false **merge** is alarming and essentially
unrecoverable — no split UI, no provenance log. So thresholds should be asymmetric, and mine are backwards.

---

## NER & query (105–112)

**105. What NER model, really?**
**BERT-tiny**: `BertForTokenClassification`, **2 layers, hidden 128**, 4,370,424 params, 16.94 MB, FP32, 7 labels,
fixed sequence length 32. **Not DistilBERT**, despite the README.

**106. Fastest way to prove it isn't DistilBERT?**
DistilBERT has **no `token_type_ids` input** — it removed segment embeddings. `ner.cpp` passes three inputs
including `token_type_ids`. The code itself proves it.

**107. Why is tiny the right size?**
It's on the **search critical path**, ships inside an APK already carrying ~300 MB of CLIP, and the task is
4 classes over ~10 words of near-templated text. Nothing like general NER over news.

**108. What are the labels?**
`O`, `B/I-SELF`, `B/I-PERSON`, `B/I-RELATION`. **No LOCATION or EVENT** — "beach" comes back `O` and goes to CLIP.
That split is deliberate: the graph owns people, CLIP owns everything else.

**109. Why BIO rather than a binary is-entity flag?**
BIO can represent two adjacent distinct entities; a binary flag merges them.

**110. Subword alignment?**
Use the **first** subword's label per word — the standard HuggingFace convention and almost certainly how the model
was fine-tuned. Taking the last, or majority-voting, would disagree with training.

**111. How do spans become entity IDs?**
Strip "my " and "'s", then Damerau-Levenshtein ≤ 2 against both `display_name` and `relation`, tie-broken by
`sample_count`.
**↳** *Problem?* Distance 2 on short strings is dangerously loose — "dad" → "mom" is distance 2, and "mom" → "tom"
is 1. It should scale with length.

**112. What if the NER model fails to load?**
Non-fatal by design. Search falls back to word-by-word fuzzy resolution, then to global KNN.
**↳** *So how much is NER worth?* **[UNKNOWN]** — I have no eval harness, and `setSearchOptions` already gives me
the A/B switch.

---

## Database, graph & search (113–120)

**113. Why SQLite?**
Embedded, ACID, zero deployment. And **Android's SQLite can't load extensions**, so to get `vec0` at all I had to
compile my own amalgamation into `libvanta.so` with `SQLITE_CORE` + `sqlite3_auto_extension`.

**114. Why WAL?**
Readers don't block the writer and the writer doesn't block readers — exactly the interactive-search-during-a-
multi-hour-index workload.

**115. Why sqlite-vec over FAISS?**
FAISS would win on raw ANN throughput, but it's **an index, not a database**. Indexing one photo writes a CLIP
vector, N face vectors, entities, centroids, memberships and graph edges — that must be one transaction. Two stores
means two durability models and manual reconciliation after every crash.

**116. Is the KNN exact or approximate?**
**Exact brute force.** Only `sqlite-vec.c` is in the CMake list; `sqlite-vec-diskann.c` and the IVF sources are in
`thirdparty/` and never compiled.

**117. When does that become a problem?**
Around 100k–500k vectors, and **only on the global-search path** — any query that resolves an entity scores ~40
candidates instead of the whole library.

**118. How is the graph stored?**
`entity_memberships` (person→file), `entity_relations` (person↔person with a co-occurrence count, canonicalised by
`CHECK(entity_a < entity_b)`), and `entity_relation_files` as a materialised (pair→file) index that makes two-person
queries an O(1) primary-key probe.

**119. Are you implementing GraphRAG?**
**No.** GraphRAG uses an LLM to extract a typed knowledge graph from text, runs Leiden community detection,
LLM-summarises communities, and *generates* answers. I have no LLM, no communities, no summaries, and no generation.
What I took is the architecture: extract entities, resolve to graph nodes, filter structurally, rank semantically.
**Inspired by, not an implementation of.**

**120. What's the biggest thing you'd fix tomorrow?**
Two preprocessing bugs — the aspect-ratio squash and the missing colour swap — because they silently degrade every
embedding I've computed and cost nothing to fix. Then the evaluation harness, because without it every subsequent
decision is a guess.
