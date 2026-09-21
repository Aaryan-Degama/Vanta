# 12 — BERT NER & Query Understanding

## The model Vanta actually ships [VERIFIED-REPO — ONNX graph inspection]

> 🔴 The README and `Reads/09` say **"DistilBERT NER"**. It is not DistilBERT.

| Property | Verified value | How |
|---|---|---|
| Class | `transformers.models.bert.modeling_bert.**BertForTokenClassification**` | embedded `pkg.torch.onnx.class_hierarchy` metadata in the ONNX file |
| Encoder layers | **2** (`bert.encoder.layer.0`, `bert.encoder.layer.1` — that's all) | initializer names |
| Hidden size | **128** (`bert.embeddings.word_embeddings.weight` = `[30522, 128]`) | initializer shapes |
| Intermediate (FFN) size | **512** (`layer.0.intermediate.dense.bias` = `[512]`) | initializer shapes |
| Attention heads | **2** (standard for this config) | — |
| Vocab | **30,522** (bert-base-uncased WordPiece) | `[30522, 128]`; `wc -l vocab.txt` = 30522 |
| Max positions | 512 | `position_embeddings.weight` = `[512, 128]` |
| Labels | **7** (`classifier.bias` = `[7]`) | initializer shapes |
| **Total parameters** | **4,370,424** | sum over all initializers |
| File size | **16.94 MB** (FP32, unquantised) | `ls -l` |
| Opset | 18 | `opset_import` |
| Inputs | `input_ids`, `attention_mask`, `token_type_ids`, each `[batch, 32]` int64 | graph inputs |
| Output | `logits` `[1, 32, 7]` float32 | graph output |

**This architecture is `google/bert_uncased_L-2_H-128_A-2` — universally known as BERT-tiny**
(Turc et al., *"Well-Read Students Learn Better"*, <https://arxiv.org/abs/1908.08962>), fine-tuned for token
classification.

### Why the "DistilBERT" claim is checkable in 30 seconds

| | DistilBERT | BERT-tiny (what Vanta ships) |
|---|---|---|
| Layers | 6 | **2** |
| Hidden | 768 | **128** |
| Parameters | ~66 M | **4.37 M** |
| FP32 size | ~265 MB | **16.94 MB** |
| `token_type_ids` input | **Does not have one** — DistilBERT removed segment embeddings | **Has one**, and Vanta feeds it |

That last row is decisive and is the fastest way to spot it: `ner.cpp` passes three inputs including
`token_type_ids`. DistilBERT's forward signature has no `token_type_ids` at all. So the code itself proves the model
is not DistilBERT.

> **How to answer:** *"The README is wrong — that's a stale label from an early plan. It's a 2-layer, 128-hidden
> BERT-tiny token classifier, 4.4 million parameters, and I can show you the ONNX initializer shapes. And tiny is the
> right call here, which I'll explain — but I should fix the README."*

### Why BERT-tiny is actually the right choice — lead with this

> "The task is extremely narrow: tag at most ~10 words of a photo-search query into 4 classes. The syntax is
> near-templated — 'photos of X and Y at Z'. That is nothing like general-purpose NER over news text, where you need
> world knowledge and DistilBERT's capacity earns its keep.
>
> The constraints point hard the other way. This model runs **on the search critical path**, so its latency is
> user-visible in a way CLIP's indexing cost is not. It ships inside the APK, where every megabyte competes with
> ~300 MB of CLIP weights. And it runs on a phone CPU alongside everything else.
>
> BERT-tiny is ~15× smaller than DistilBERT and roughly an order of magnitude cheaper per inference, and for a
> 4-class, 10-token, templated tagging problem I'd expect it to be within a point or two of DistilBERT — though I
> should say plainly that **I have no held-out F1 measurement to prove that**, which is a real gap. Given the size of
> the model, I'd go further: this task is simple enough that a well-built rule system plus a gazetteer of the user's
> own entity names might match it. The reason I still prefer the model is generalisation to phrasings I didn't
> anticipate."

---

## NER from first principles

**Named Entity Recognition** = per-token classification over a sequence. Not a parser, not a classifier over the
whole sentence — a label for every token, decoded into spans.

### BIO tagging

`B-X` begins an entity of type X, `I-X` continues it, `O` is outside. This makes *adjacent distinct entities*
representable, which a naive "is this token an entity?" scheme cannot do:

```
photos   of    my    mom    and    aaryan   at   the   beach
  O      O     O    B-REL    O    B-PERSON  O    O      O
```

### Vanta's label set [VERIFIED-REPO — `label_map.json`]

```json
{"0":"O", "1":"B-SELF", "2":"I-SELF",
 "3":"B-PERSON", "4":"I-PERSON",
 "5":"B-RELATION", "6":"I-RELATION",
 "max_len": 32}
```

**Exactly three entity types. There is no LOCATION, EVENT, DATE, or OBJECT label.**

| Label | Meaning | Downstream handling in `query_engine.cpp` |
|---|---|---|
| `SELF` | a self-reference: *me, myself, my* | resolved to `g_owner_entity_id` (set by the user in Settings). If no owner is set, the span is pushed back into the CLIP caption |
| `PERSON` | a proper name: *aaryan, divy* | fuzzy-resolved against `entities.display_name` |
| `RELATION` | a kinship/social term: *mom, dad, brother* | fuzzy-resolved against `entities.relation` **and** `display_name` |
| `O` | everything else | becomes the residual text → Intent Builder → CLIP |

> ⚠️ **Do not invent labels.** `beach`, `graduation`, `temple` are **not** tagged. They fall out as `O`, which is
> exactly the design: *the graph owns people, CLIP owns everything else*. That split is clean and you should present
> it as deliberate, because it is.

Worked example against the real label set:

```
"photos of me with mom at the beach"
 photos → O
 of     → O
 me     → B-SELF      ──► owner_entity_id
 with   → O
 mom    → B-RELATION  ──► resolve("mom") → entity_id
 at     → O
 the    → O
 beach  → O           ──► residual → IntentBuilder → "at a beach" → CLIP
```

---

## The tokenizer [VERIFIED-REPO — `bert_tokenizer.cpp`]

A hand-written WordPiece implementation:

```
normalize()        lowercase alphas; keep digits/whitespace/ASCII punctuation; replace everything else with ' '
basic_tokenize()   split on whitespace; split punctuation into its own token
wordpiece()        greedy LONGEST-MATCH-FIRST:
                     try the whole word; on failure drop the last char and retry;
                     on a match, prefix the remainder with "##" and continue;
                     if nothing matches, emit [UNK] and abandon the word
encode()           [CLS]=101 ... [SEP]=102, pad to 32 with 0, attention_mask 1 for real tokens,
                   token_type_ids all 0, and word_ids[] mapping each subword back to its source word
```

This is a faithful WordPiece. Two notable properties:

- **The vocab file format is custom.** `load()` requires **tab-separated** `token\tid` lines, not HuggingFace's
  one-token-per-line `vocab.txt` (where the id is the line number). The repo's
  `.model/vocab.txt` **is** pre-converted — all 30,522 lines contain a tab — so this is consistent, not a bug.
  But it is a silent coupling: dropping in a stock HF `vocab.txt` would make every line fail the
  `find('\t')` check, leave `vocab_` empty, make `load()` return false, and **silently disable NER** (the caller
  treats NER load failure as non-fatal). Worth knowing.
- **Non-ASCII is dropped**, replaced by spaces in `normalize()`. Combined with `tokenize_chinese_chars: true` in
  `tokenizer_config.json`, this means the shipped tokenizer config and the C++ implementation disagree about
  non-Latin input. Vanta is English-only in practice.

### The subword-to-word alignment — a detail worth knowing

A word may become several subword tokens (`"aaryans"` → `aaryan`, `##s`). The model emits a label per *token*, but
spans are over *words*. `ner.cpp` handles this correctly:

```cpp
for (int i = 0; i < seq_len; ++i) {
    int wid = word_ids[i];
    if (wid < 0) continue;                       // skip [CLS], [SEP], [PAD]
    if (i > 0 && word_ids[i-1] == wid) continue; // use ONLY the first subword of each word
    // argmax over the 7 logits → id2label → per_word_labels[wid]
}
```

**Using the first subword's label** is the standard convention (it's what HuggingFace's
`tokenize_and_align_labels` does with `label_all_tokens=False`, and it's almost certainly how this model was
fine-tuned). Getting this right matters: taking the *last* subword, or majority-voting, would systematically
disagree with the training-time alignment. ✅

### BIO decoding

`decode_spans()` handles the three cases correctly: `B-X` always starts a new span (closing any open one), `I-X`
continues only if the type matches and otherwise starts a new span (graceful handling of invalid sequences like a
bare `I-PERSON`), and `O` closes. A trailing open span is flushed. ✅

---

## Entity resolution — from a span to an `entity_id`

NER gives you `"mom"`. The graph needs `entity_id = 7`. That mapping is **`resolve_span_to_entity_id`**:

```cpp
std::string lower = to_lower(span_text);
if (lower.substr(0,3) == "my ") lower = lower.substr(3);   // "my mom" → "mom"
lower = strip_possessive(lower);                            // "mom's"  → "mom"

int best_dist = 3; int64_t best_id = -1; int best_count = 0;
for (const auto& c : candidates) {
    int d = std::min(damerau_levenshtein(lower, to_lower(c.display_name)),
                     c.relation.empty() ? 999 : damerau_levenshtein(lower, to_lower(c.relation)));
    if (d < best_dist || (d == best_dist && c.sample_count > best_count)) {
        best_dist = d; best_id = c.entity_id; best_count = c.sample_count;
    }
}
return best_dist <= 2 ? best_id : -1;
```

**Damerau-Levenshtein** = Levenshtein (insert/delete/substitute) **plus transposition** as a single edit, so
`"mmo"` → `"mom"` costs 1, not 2. That matters for typing on a phone keyboard.

**Design notes:**
- Matching against **both** `display_name` and `relation` is what makes *"photos of mom"* and *"photos of Priya"*
  resolve to the same entity. Good.
- The `sample_count` tiebreak means *"when two people are equally close, prefer the one who appears more often"* —
  a sensible prior.
- ⚠️ **Edit distance ≤ 2 is dangerously loose on short strings.** `"mom"` → `"tom"` is distance 1. `"dad"` → `"mom"`
  is distance 2 — **so a user named Tom could be returned for "mom", and "dad" could resolve to "mom"**. The
  threshold should scale with length: something like `max_dist = len <= 4 ? 1 : 2`. This is a concrete, easy bug to
  volunteer.
- The `owner_entity_id` parameter is accepted and **ignored** (`int64_t /*owner_entity_id*/`) — dead parameter.

### The fallback path

If NER yields no resolvable spans, `fallback_entity_resolution()` walks the raw query word by word, skips a
28-word filler list and any word shorter than 3 characters, and tries `resolve_span_to_entity_id` on each. This
rescues the case where the NER model is missing, failed to load, or mis-tagged — and it's why search still works at
all if `ner_model.onnx` is absent.

> Note the consequence: **with the fallback, the NER model is not strictly required.** That's a good robustness
> property and also an honest observation about how much the model is really contributing — a question worth being
> ready for: *"How much worse is search with NER disabled?"* The truthful answer today is **"I don't know, because I
> have no evaluation harness"** — and then describe the A/B you'd run, since `setSearchOptions(useGraph, …)` already
> gives you the switch.

---

## Why BERT-based NER instead of simpler methods?

| Approach | Why it's insufficient |
|---|---|
| **Regex** | `"(?:my )?(mom|dad|...)"` handles the templated cases but breaks on word order, on names it's never seen, and on any phrasing you didn't enumerate. Every new pattern is a code change |
| **Keyword / gazetteer matching** | Can't distinguish *roles*: in *"me and mom"*, both are people, but only one is the owner. Can't handle multi-word names. Can't tell a person-word from a scene-word that happens to also be a name (*"Rose"*) |
| **Exact string matching** | Zero typo tolerance. Phone keyboards guarantee typos |
| **Hard-coded person names** | Impossible — the names come from the user's own library at runtime |
| **Pure embedding search** | The whole point: CLIP cannot resolve identity. See [09](./09_CLIP_AND_IMAGE_TEXT_RETRIEVAL.md) |

**What the model genuinely buys:** *contextual* disambiguation. Self-attention lets the representation of a token
depend on its neighbours, so the model can learn that in *"photos of rose at the park"* → `rose` is B-PERSON, while
in *"a rose in the garden"* → `rose` is O. No gazetteer can do that, because the surface string is identical.

### The honest weaknesses

| Weakness | Effect in Vanta | Mitigation present? |
|---|---|---|
| **Unseen entities** | A name the model never saw may be tagged `O` | ✅ the word-by-word fallback |
| **Ambiguity** | *"Rose"* the person vs the flower | ⚠️ the model is the mitigation, and it's tiny |
| **Pronouns / coreference** | *"her graduation"* — no antecedent to resolve | ❌ not handled at all |
| **Misspellings** | *"mmo"* | ✅ two independent defences: SymSpell-style correction *before* NER, and Damerau-Levenshtein ≤ 2 *after* |
| **Very short queries** | *"mom"* alone gives almost no context for a contextual model | ✅ fallback |
| **Unusual phrasing** | trained on a narrow synthetic distribution | ❌ **the core risk of a 4 M-param model** |
| **Multilingual** | `normalize()` discards non-ASCII; vocab is uncased English | ❌ English only |
| **Domain shift** | The training data isn't in the repo, so the true distribution is unknown | ❌ **unverifiable** |
| **Fixed seq len 32** | Longer queries are truncated (correctly, keeping `[SEP]`) | ✅ fine for this use case |

> ⚠️ **The training data and training script are not in this repository.** You cannot show an interviewer how the
> model was trained, what data it saw, or its held-out F1. If asked *"how well does your NER work?"*, the only
> honest answer is a description of how you'd measure it — a held-out set of realistic queries, per-label precision/
> recall/F1, plus end-to-end Recall@10 with NER on vs off. Do not invent a number.

---

## The conceptual decomposition — your strongest single explanation

Learn this. It is the clearest articulation of why Vanta is built the way it is, and it generalises far beyond this
project:

> "Three different questions, three different tools, and none of them can do another's job.
>
> **The NER model answers 'what entities are mentioned?'** It's a language problem. It turns an unstructured
> sentence into structured references — *me*, *mom* — and it needs context to do it, which is why it's a transformer
> and not a regex.
>
> **The graph answers 'how do those entities relate to my stored data?'** It's a *set* problem. Which files contain
> entity 7? Which contain 7 **and** 12 together? That's an exact, conjunctive, relational question, and relational
> databases are extremely good at it. It's also where negation lives — *'photos without my brother'* is a `NOT EXISTS`,
> and there is no embedding that expresses it.
>
> **CLIP answers 'which of these look like the described scene?'** It's a *similarity* problem — graded, fuzzy,
> open-vocabulary, and unable to represent identity.
>
> **Reranking combines them**, and the ordering is the design: the graph is a **hard filter**, CLIP is a **soft
> ranker**. Filter first, because it's exact and it shrinks the candidate set from 100,000 to 40; rank second,
> because it's approximate and expensive.
>
> The failure of the naive design is that it tries to make one embedding space answer all three. You end up asking a
> similarity function to enforce a set constraint, and it silently returns things that are *nearly* right — which for
> 'photos of my mother' is exactly the wrong failure mode."
