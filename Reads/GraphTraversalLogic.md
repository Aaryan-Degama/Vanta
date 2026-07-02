# Vanta Graph Search & Traversal Logic

Vanta uses a hybrid search pipeline that combines natural language processing (NER) with a relational Face Graph structure stored in SQLite. This allows the engine to accurately parse natural language queries, traverse the entity graph, and filter visual results.

Here is a step-by-step breakdown of how the graph traversal works and how the results are surfaced:

## 1. Intent & Span Extraction (NER)
When a query arrives (e.g., *"pictures of my dad and mom at the beach"*), it first goes through the **NER (Named Entity Recognition)** model.
- The model extracts labeled spans (e.g., `[dad] -> RELATION`, `[mom] -> RELATION`, `[beach] -> UNRESOLVED/CLIP_CAPTION`).

## 2. Span Resolution (String -> Entity ID)
The extracted spans need to be mapped to actual face graph entities in the SQLite database.
- The system pulls all known entities from the `entities` table.
- `resolve_span_to_entity_id()` calculates the **Damerau-Levenshtein distance** between the extracted span (e.g., "dad") and the database fields (`display_name` and `relation`).
- The system strictly matches spans that are within an edit distance of `2`. If multiple entities tie, the one with the highest `sample_count` (most common face) wins.
- This resolves our spans into concrete SQLite `entity_id`s.

## 3. Graph Traversal & Candidate Intersection
Once we have a set of requested `entity_id`s, we traverse the graph to find files where these entities coexist:
- **1 Entity:** Query the `entity_memberships` table directly (`SELECT file_id WHERE entity_id = ?`).
- **2 Entities:** Query the pre-computed `entity_relation_files` edge table, which natively stores files where both entities co-occur.
- **3+ Entities:** The engine queries `entity_memberships` for *each* entity individually and computes an intersection of the resulting `file_id` sets in memory. 

This guarantees that we only look at photos where *all* requested people are physically present together.

## 4. Multi-Entity Scoring & Re-ranking
After gathering the candidate `file_id`s from the graph, the system ranks the results:

### Scenario A: Query contains visual context (e.g., "...at the beach")
If there is remaining text after extracting the people, the text is sent to the Intent Builder to generate a **CLIP Caption**.
- The system generates a CLIP text embedding for the caption.
- It then calculates the Cosine Similarity (Dot Product) between the text embedding and the image embeddings *only* for the candidate files returned by the graph traversal.
- **Strict Verification:** Before adding a file to the final results, `verify_entities_in_file` runs to ensure that every requested `entity_id` is definitively detected in that specific file (preventing false positives).
- Results are ranked by **CLIP distance**.

### Scenario B: Query is *only* people (e.g., "my dad and mom")
If there is no visual context, CLIP is bypassed entirely to save resources.
- The system calls `get_combined_membership_scores()`.
- For each candidate file, it looks up the facial detection confidence score (`det_score` in `entity_memberships`) for each requested entity.
- The file's final score is the **minimum score** among the required entities (meaning a photo is only ranked as high as its lowest-confidence face).
- Results are ranked by this combined graph membership score.

## Summary
The traversal moves from **Text -> NER Spans -> Entity IDs -> Graph Edge Intersection -> Contextual Re-ranking**. This architecture allows Vanta to instantly narrow down thousands of photos to just the handful where specific family members are together, before using AI to find the exact visual context.
