# 27 — Project Story & Personal Contribution

## Part 1 — The four answers

### 15 seconds: "What is Vanta?"

> "Vanta is an Android app that lets you search your own photos by typing a sentence — including by person, like
> 'me and my mom at the beach'. Everything runs on the device: CLIP for scene understanding, ArcFace for faces, and
> a small entity graph in SQLite. No cloud, no network permission at all."

### 60 seconds

> "Vanta solves the problem that your phone has 50,000 photos indexed by date and filename, but you remember them as
> *'me and mom at the beach'*.
>
> It's a React Native app over a C++ engine. During indexing it runs three ONNX models on-device: CLIP ViT-B/32 for
> a 512-dimensional semantic embedding per photo, an InsightFace SCRFD detector plus an ArcFace ResNet50 recogniser
> for faces, and it clusters the face embeddings into person entities you can name.
>
> At search time a small BERT token classifier tags which words are people, those resolve to entity IDs, the entity
> graph gives me the files containing those specific people together, and CLIP reranks that candidate set by how
> well it matches the rest of the sentence.
>
> **The core idea is that identity is a filter and semantics is a ranker.** CLIP fundamentally can't tell you *which*
> person is in a photo, and a vector dot product can't express 'contains these two people'. So you need two indexes
> and something that decides which one answers which part of the query.
>
> And everything is offline by construction — the app doesn't request the `INTERNET` permission, which is also why
> every other constraint in the project is hard."

### 3 minutes

> *(Start with the 60-second version, then:)*
>
> "Let me go one level deeper on the three interesting parts.
>
> **The pipeline.** Indexing runs in an Android foreground service so it survives backgrounding. Progress lives in a
> `status` column in SQLite rather than in memory, so if the app is killed at photo 30,000 it resumes at 30,000 with
> nothing redone. Per photo: decode, CLIP embed, face detect with a SCRFD decoder I wrote by hand from the output
> tensor shapes, 5-point similarity-transform alignment to 112×112, ArcFace embed each face, cluster, and update the
> co-occurrence graph.
>
> **The storage.** Everything is one SQLite file with the sqlite-vec extension, which I had to compile myself —
> Android's SQLite API can't load extensions, so getting vector search at all is what forced a native engine. The
> payoff is that vectors and relational data live in one file under one transaction: indexing a photo writes a CLIP
> vector, face vectors, entities, memberships and graph edges, and that's atomic. With a separate FAISS index I'd
> have two durability models and manual reconciliation after every crash.
>
> **The search.** Eight stages, with fallbacks at every one — spell correction, NER, entity resolution, candidate
> selection from the graph, CLIP reranking, face verification, burst dedup. If NER fails to load, search degrades to
> word-by-word fuzzy matching. If no entity resolves, it degrades to a global vector search. That layering is
> deliberate.
>
> **And I'd tell you the biggest weakness**, because it's the part I understand best now: I represent each person
> with a single averaged vector. A person's embeddings are multi-modal — frontal versus profile, with and without
> glasses, at different ages — so the average lands between the modes, where no real face of that person lives.
> That makes it simultaneously too far from the right faces and too close to the wrong ones. Apple's published
> approach uses a set of canonical exemplars instead, and that's the change I'd make first."

### 10 minutes — the architecture walkthrough

**Minute 1–2: the problem.** Open vocabulary rules out a classifier. Identity rules out CLIP alone. Relational
constraints rule out a single vector index. Offline rules out every easy answer. *(→ [02](./02_PROJECT_OVERVIEW.md))*

**Minute 2–3: the stack, top to bottom.** RN/TS → Expo module → Kotlin → JNI → C++ → SQLite. Emphasise the boundary
is crossed **once per operation**, not per item: indexing 50,000 photos is three JNI calls.
*(→ [03](./03_SYSTEM_ARCHITECTURE.md), [06](./06_JNI_BRIDGE.md))*

**Minute 3–5: indexing.** MediaStore scan → C++ loop → CLIP FP16, SCRFD+ArcFace INT8 → online clustering → graph
edges. Mention the precision split and why: convnets quantise to INT8 well and need no calibration data (which you
can't collect by design); ViTs are delicate, so FP16 for size.
*(→ [16](./16_INDEXING_PIPELINE.md), [08](./08_ONNX_RUNTIME.md))*

**Minute 5–7: search.** Trace "photos of me and mom at the beach" through all eight stages. Land on the
filter-then-rank argument and the numbers: two indexed lookups plus 40 dot products versus 100,000 dot products.
*(→ [14](./14_SEARCH_PIPELINE.md))*

**Minute 7–8: the models, briefly but precisely.** CLIP ViT-B/32 — contrastive objective, shared space, why the
export is correct. buffalo_l — SCRFD-10GF and ResNet50 ArcFace, and why the angular margin is what makes open-set
clustering possible. BERT-tiny — 2 layers, 128 hidden, 4.4M params, and why tiny is right for a 4-class, 10-word,
near-templated task on the latency-critical path.
*(→ [09](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md), [10](./10_FACE_RECOGNITION.md), [12](./12_BERT_NER_QUERY_UNDERSTANDING.md))*

**Minute 8–9: what's wrong with it.** The centroid problem. The two preprocessing bugs. No evaluation harness. Lead
with these; don't wait to be asked. *(→ [01](./01_VANTA_REALITY_CHECK.md))*

**Minute 9–10: what's next.** Fix preprocessing → build the eval harness → exemplar galleries + quality gating →
MobileCLIP. And say explicitly: *"I wouldn't add more ML. My problems are an unvalidated representation choice, two
preprocessing bugs, a missing index, and no way to measure any of it."*
*(→ [22](./22_ARCHITECTURAL_IMPROVEMENTS.md))*

---

## Part 2 — What you can legitimately claim

### Claims supported by repository evidence

Git history: **53 commits, 2026-06-10 → 2026-07-03**. `git shortlog -sne` gives 41 commits as `Aaryan-Degama` plus
13 as `Degama Aaryan Jitendrakumar` (the same person, GitHub's noreply address, mostly merge commits) — **54 of 57
author entries, ~95%**. Divy-21 has 3 commits: a folder restructure, AndroidManifest permissions, and one touch to
`vanta.cpp`.

Per-file authorship on the core engine:

| File | Commits by you | Commits by others |
|---|---|---|
| `vanta.cpp` (979 lines) | 18 | 1 (Divy-21) |
| `query_engine.cpp` (891) | **12** | **0** |
| `Seg_model.cpp` (559) | **7** | **0** |
| `face_DB.cpp` (683) | 7 | 0 |

**So you can say, accurately:**

- ✅ "I designed and built the complete system end-to-end over about three weeks."
- ✅ "I wrote essentially all of the ~4,500-line C++ engine — the JNI layer, the query engine, the face pipeline,
  the clustering, the graph, and the database layer."
- ✅ "I hand-wrote the SCRFD anchor decoder from the model's output tensor shapes — strides, two anchors per
  location, distance-to-box decoding, keypoint offsets, NMS, and inverting the letterbox transform."
- ✅ "I implemented the ArcFace 5-point alignment with the canonical landmark template and a similarity transform."
- ✅ "I wrote the CLIP BPE tokenizer and the BERT WordPiece tokenizer in C++ from scratch."
- ✅ "I designed the schema — the entity graph, the membership and co-occurrence tables, the materialised
  edge-to-file index — and the retrieval pipeline over it."
- ✅ "I did the ONNX export and quantisation pipeline: CLIP to FP16, the InsightFace models to INT8 dynamic."
- ✅ "I built the Expo native module, the JNI bridge, the MediaStore scanner, and the foreground indexing service."
- ✅ "I built all six React Native screens."
- ✅ **"I audited my own repository and found the bugs I'm telling you about, including three I confirmed by
  executing the real schema."**

### Claims that need care

| Claim | Why care | Say instead |
|---|---|---|
| "I trained the NER model" | The training script and data are **not in the repo**. You have a fine-tuned checkpoint | "I fine-tuned a BERT-tiny token classifier for this label set" — **only if you actually did**. If you didn't, say where it came from |
| "It's cross-platform" | iOS is a 15-line stub | "The C++ core is portable by construction; only Android is wired up" |
| "Search takes 280 ms" | No harness, no recorded results | "I measured it informally from my own timing logs; I don't have a reproducible benchmark" |
| "It indexes documents" | Not implemented | "It discovers them; embedding them is future work" |
| "I use GraphRAG" | You don't | "The query architecture is inspired by GraphRAG's ideas" |
| "DistilBERT" | It's BERT-tiny | Just say BERT-tiny and give the parameter count |
| "I built it with a teammate" | Divy-21 has 3 commits | "A collaborator contributed the early folder restructure and the manifest permissions" — accurate and generous |

### The AI-assistance question

Your git history contains a commit titled **"Kimi updated the code quality"** and an early **"Removed .claude"**.
An interviewer browsing your commits will see them.

**Do not hide this, and do not be defensive.** The honest, strong position:

> "I used AI assistance for parts of this — mostly refactoring, comments and boilerplate, and there's a commit in
> my history that says so. What I'd point at instead is that I can explain any line in the repository, including
> the parts I'd change. I hand-wrote the SCRFD decoder and I can tell you why there are exactly 12,800 anchors at
> stride 8 and why box regression is distance-based rather than delta-based. I can tell you why alignment uses a
> similarity transform instead of a full affine, and what that has to do with preserving facial geometry. And I can
> tell you three bugs in my own code that I found by running my schema against SQLite. That's the test that
> matters."

**This is also exactly why [26](./26_CODE_LEVEL_INTERVIEW.md) exists.** The single most effective defence against
"did you really write this?" is fluency at the line level — being able to explain not just *what* a line does, but
*why it's that line and not the obvious alternative*.

---

## Part 3 — How to open, and how to steer

### The opening move

When asked "tell me about Vanta", give the 60-second version and end with a hook you want them to pull:

> "…and the part I find most interesting is that it needs two completely different indexes, because CLIP
> fundamentally can't tell you *which* person is in a photo."

That invites the question you most want to answer. Have three such hooks ready:

1. **"CLIP can't do identity"** → the architectural argument, the strongest thing in the project.
2. **"The hardest part was clustering people with no labels"** → open-set recognition, the centroid problem, Apple's
   approach.
3. **"I found a bug in my own preprocessing that degrades every embedding"** → demonstrates rigour immediately.

### Volunteer the weaknesses, early and specifically

The counterintuitive rule: **name your biggest flaw before you're asked.** It converts an interrogation into a
technical conversation, and it's the difference between "candidate defends a demo" and "engineer assesses a system".

Rank the ones to raise unprompted:
1. Single-centroid identity representation — biggest blast radius.
2. No evaluation harness — the reason you can't justify half your decisions.
3. The aspect-ratio and BGR/RGB preprocessing bugs — concrete, findable, already diagnosed.
4. iOS is a stub, Android is arm64-only.
5. Testing is four smoke tests.

### The three sentences to have loaded

**On the architecture:** *"Identity is a filter, semantics is a ranker. Filter first because it's exact and cuts
100,000 candidates to 40; rank second because it's approximate and expensive."*

**On CLIP vs ArcFace:** *"CLIP is a scene model, ArcFace is an identity model, and contrastive training makes CLIP
actively worse at identity — two different men at two beaches share the caption 'a man at the beach', so the loss
pulls them together."*

**On what you learned:** *"Build the evaluation harness before the third feature, not after the last one. I ship
three search toggles whose value I cannot measure."*

### If you don't know something

> "I don't know — I never measured that. Here's how I'd find out: ..."

Then give a real method. Every **[UNKNOWN]** in this knowledge base has a measurement plan attached precisely so you
can do this. An honest "I don't know, and here's the experiment" beats a confident wrong number every single time —
and it's the one answer that's impossible to attack.
