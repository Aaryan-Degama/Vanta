# Vanta — Interview Mastery & Repository Audit

> A repository-grounded technical knowledge base for defending **Vanta** in a serious software/ML systems interview.
>
> Repository: <https://github.com/Aaryan-Degama/Vanta>
> Audit date: **2026-09-21** · Audited commit: `fa53057` (branch `main`)

---

## How to read this

Everything here was written **after reading the actual source**, not the README. Where the documentation and the
implementation disagree, **the implementation wins** and the disagreement is called out explicitly.

Every quantitative claim carries one of four labels:

| Label | Meaning |
|---|---|
| **[VERIFIED-REPO]** | Measured or read directly out of this repository (file, ONNX graph, SQL, code path) |
| **[VERIFIED-EXT]** | From a primary external source (paper, official model card, official docs), cited inline |
| **[ESTIMATED]** | A reasoned derivation from verified quantities — arithmetic is always shown |
| **[UNKNOWN]** | Not measurable from the repo. Needs a benchmark. **Never state this as fact in an interview.** |

---

## Start here (in order)

1. **[01 — Vanta Reality Check](./01_VANTA_REALITY_CHECK.md)** — what is actually built vs. what the docs claim. **Read this first and read it twice.** It contains the 12 findings most likely to end your interview badly if you meet them for the first time in the room.
2. **[27 — Project Story & Personal Contribution](./27_PROJECT_STORY_AND_CONTRIBUTION.md)** — your 15s / 60s / 3min / 10min answers.
3. **[28 — Cheat Sheet](./28_CHEAT_SHEET.md)** — the compressed everything, including **THE 25 THINGS I MUST BE ABLE TO EXPLAIN WITHOUT HESITATION**.

---

## Full index

### Foundations
| Doc | Covers |
|---|---|
| [01 — Vanta Reality Check](./01_VANTA_REALITY_CHECK.md) | Claim-by-claim audit, evidence table, 12 critical findings, risk ranking |
| [02 — Project Overview](./02_PROJECT_OVERVIEW.md) | The problem from first principles, why it's hard, why naive systems fail |
| [03 — System Architecture](./03_SYSTEM_ARCHITECTURE.md) | Every layer, every boundary: representation, ownership, threading, failure |

### The stack, layer by layer
| Doc | Covers |
|---|---|
| [04 — React Native & Expo](./04_REACT_NATIVE_EXPO.md) | Why RN, why Expo, alternatives, what Expo can't do |
| [05 — Kotlin & Android](./05_KOTLIN_ANDROID.md) | Kotlin rationale, MediaStore, scoped storage, foreground service, permissions |
| [06 — JNI Bridge](./06_JNI_BRIDGE.md) | JNI deep dive, actual call traces, crossing costs, the batching question |
| [07 — C++ Core Engine](./07_CPP_CORE_ENGINE.md) | Why C++, build system, ABI, threading model, the real race conditions |
| [08 — ONNX Runtime](./08_ONNX_RUNTIME.md) | ORT internals, execution providers, FP16 vs INT8, session lifecycle |

### The models
| Doc | Covers |
|---|---|
| [09 — CLIP & Image-Text Retrieval](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md) | Contrastive learning from scratch, Vanta's usage, SigLIP/MobileCLIP comparison, limitations |
| [10 — Face Recognition](./10_FACE_RECOGNITION.md) | SCRFD detection, 5-point alignment, ArcFace math, the exact models |
| [11 — Face Clustering & Person Segregation](./11_FACE_CLUSTERING_AND_PERSON_SEGREGATION.md) | The clustering algorithm, the centroid problem, quality scoring, v2 design |
| [12 — BERT NER & Query Understanding](./12_BERT_NER_QUERY_UNDERSTANDING.md) | The real model (BERT-tiny), tokenizer, BIO decoding, why NER over regex |

### Retrieval
| Doc | Covers |
|---|---|
| [13 — Graph & GraphRAG](./13_GRAPH_AND_GRAPHRAG.md) | The entity graph, GraphRAG inspiration vs. literal implementation, improvements |
| [14 — Search Pipeline](./14_SEARCH_PIPELINE.md) | Four queries traced end to end, reranking, score fusion, the scoring bug |
| [15 — SQLite & sqlite-vec](./15_SQLITE_AND_SQLITE_VEC.md) | Embedded DB rationale, vec0 internals, alternatives, storage math |
| [16 — Indexing Pipeline](./16_INDEXING_PIPELINE.md) | Scan → embed → cluster → graph, batching, resumability |

### Systems properties
| Doc | Covers |
|---|---|
| [17 — Performance & Scalability](./17_PERFORMANCE_AND_SCALABILITY.md) | Latency budget, memory, battery, scaling walls, bottleneck ranking |
| [18 — Failure Modes & Consistency](./18_FAILURE_MODES_AND_CONSISTENCY.md) | Crash consistency, orphan data, 14 failure scenarios, recovery design |
| [19 — Privacy, Security & Fairness](./19_PRIVACY_SECURITY_FAIRNESS.md) | Threat model, why "on-device" ≠ "secure", demographic bias |
| [20 — Testing & Evaluation](./20_TESTING_AND_EVALUATION.md) | What exists (little), what's needed, retrieval and face metrics |

### Research & redesign
| Doc | Covers |
|---|---|
| [21 — Apple Photos Comparison](./21_APPLE_PHOTOS_COMPARISON.md) | Apple's person-recognition research mapped onto Vanta, + Person Recognition v2 |
| [22 — Architectural Improvements](./22_ARCHITECTURAL_IMPROVEMENTS.md) | Vanta v2, tiered by cost, full decision table |

### Interview drilling
| Doc | Covers |
|---|---|
| [23 — Interview Questions](./23_INTERVIEW_QUESTIONS.md) | Long-form Q&A with model answers and follow-ups |
| [24 — Rapid Fire](./24_RAPID_FIRE.md) | 120 questions, 2–5 sentence answers, follow-ups |
| [25 — Hostile Interview Mode](./25_HOSTILE_INTERVIEW.md) | Every buzzword you might say, and the attack on it |
| [26 — Code-Level Interview](./26_CODE_LEVEL_INTERVIEW.md) | "Why is this line like this?" with real snippets |
| [27 — Project Story & Contribution](./27_PROJECT_STORY_AND_CONTRIBUTION.md) | 15s/60s/3min/10min + what you can legitimately claim |
| [28 — Cheat Sheet](./28_CHEAT_SHEET.md) | Compressed reference + the 25 must-know concepts |

---

## The one-paragraph truth about Vanta

Vanta is an **Android-only** (arm64-v8a only), on-device photo search engine. A React Native / Expo UI calls a Kotlin
Expo module, which calls a ~4,500-line C++ engine over JNI. The engine runs three ONNX models on CPU via ONNX Runtime
1.26.0: **CLIP ViT-B/32** (FP16) for image-text semantics, **InsightFace buffalo_l** (SCRFD-10GF detector + ResNet50
ArcFace recognizer, both INT8-quantized) for faces, and a **2-layer BERT-tiny** token classifier for query NER.
Everything persists in one SQLite file with the **sqlite-vec** extension providing brute-force KNN over 512-d vectors.
Face embeddings are clustered online into person entities; co-occurrence between entities forms a small graph; a
natural-language query is spell-corrected, NER-tagged, resolved to entity IDs by edit distance, used to select a
candidate file set from the graph, and finally reranked by CLIP cosine similarity. It works, it is genuinely
private, and it has **real, findable weaknesses** — which is exactly why it is a good interview project, provided
you find them before the interviewer does.
