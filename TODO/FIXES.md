# Vanta — Fix Tracker

Actionable checklist. Source of truth for *why* each item matters is
`Interview_Prep/01_VANTA_REALITY_CHECK.md` and `Interview_Prep/22_ARCHITECTURAL_IMPROVEMENTS.md` —
this file only tracks done/not-done. Check a box when merged to main.

## Now (bugs, hours, ship before anything else)

- [ ] Write real `entity_memberships.score` — cosine is already computed in `cluster_faces_for_file`'s
      `MATCH` query, just add it to the INSERT. `face_DB.cpp`. [01 §A1, 22 #4]
- [ ] Fix `entity_id = -1` tombstone — violates FK (`entities` has no id -1), UPDATE silently rejected,
      unmatched faces get reprocessed on every index run forever. Use a `rejected_at` column or a
      `state` enum instead. `face_DB.cpp::recluster_pending_faces`. [01 §A2]
- [ ] Re-point `entity_relations`/`entity_relation_files` edges before deleting a merged entity —
      FK cascade currently wipes co-occurrence history on every merge. `face_DB.cpp::merge_similar_entities`.
      [01 §A3]
- [ ] Fix CLIP aspect-ratio squash — `cv::resize` to 224x224 anisotropically distorts every photo;
      resize short side + center-crop instead. `CLIP_image.cpp::load_img`. Degrades 100% of the index. [22 #1]
- [ ] Add `BGR->RGB` swap before face detection (SCRFD trained on RGB). `Seg_model.cpp::get_faces`. [22 #2]
- [ ] Intent Builder: append unmatched residue instead of dropping it — "my dog at the beach" -> "at a beach"
      loses "dog". `IntentBuilder::buildIntent`. [01 §C1, 22 #7]
- [ ] Move tokenizer load inside `g_session_mutex` — real data race on concurrent cold-start searches.
      `vanta.cpp`. [01 §C3, 22 #8]
- [ ] `sqlite3_busy_timeout(db, 5000)` on every `sqlite3_open*` — zero busy handling anywhere. [22 #5]
- [ ] Add indexes on `face_detections` — `verify_entities_in_file` full-scans a 150k-row table on the
      search path. `init_face_schema`. [22 #6]
- [ ] `android:allowBackup="false"` in the manifest — face DB can otherwise reach Google Drive. [22 #9]

## Soon (correctness / cleanup, still small diffs)

- [ ] Decode each image once (currently `imread` for CLIP + separate `ifstream`+`imdecode` for faces).
      `generateEmbeddingsNative`. [22 #3]
- [ ] Stop writing a 320px crop per face (~4.5GB at 100k photos); generate avatar crop lazily from stored
      bbox. `run_face_pipeline`. [22 #10]
- [ ] Replace `system("rm -rf ...")` with `std::filesystem::remove_all`. `resetFaceDataNative`. [22 #11]
- [ ] Cache ORT input/output names instead of re-querying per inference / per face. `Seg_model.cpp`,
      `CLIP_model.cpp`. [22 #12]
- [ ] Check `sqlite3_step` return codes (~15 discarded call sites) — this is *why* A1/A2/A3 above are
      invisible in logs. [22 #22]
- [ ] Fix CI: `.github/` lives under `vanta_app/`, GitHub Actions only reads it at repo root; also never
      runs `expo prebuild`, and `android/` is gitignored. [22 #27]
- [ ] Fix README: drop the document-indexing claim (not implemented at all), correct "DistilBERT" ->
      BERT-tiny (2-layer, 4.4M params), qualify/drop the "~280ms avg latency" number (no benchmark harness
      exists). [22 #13, 01 §B3]
- [ ] `files.display_name` is always empty — JNI reader never reads that Kotlin field.
      `vanta.cpp::startStoring`. [01 §B3]

## Later (real engineering effort, needs the eval harness first)

- [ ] Build an evaluation harness (synthetic labelled library + queries + Recall@10/NDCG@10) — everything
      below this line is unfalsifiable without it. [22 #24]
- [ ] Quality scoring + gating on face crops (size/sharpness/pose/exposure composite Q) — unlocks a real
      membership score, better avatars, adaptive thresholds. [11 §Part 4, 22 #14]
- [ ] Exemplar galleries instead of single centroid per person — current single-EMA-centroid is flagged as
      "worst decision" in the design (fragmentation + false merges). [21, 22 #15]
- [ ] Invert merge/assign thresholds — merging (cos 0.50) is currently looser than assigning (cos 0.82) and
      merging is the irreversible one. [22 #16]
- [ ] Per-photo injective assignment (Hungarian) — a person appears at most once per photo, currently
      unenforced. [22 #17]
- [ ] Feed co-occurrence graph back into clustering as a cannot-link constraint. [22 #18]
- [ ] Batch ONNX inference (4-8) — both graphs already declare dynamic batch dims. [22 #19]
- [ ] One transaction per photo instead of four; set `status='indexed'` last, inside it. [22 #21]
- [ ] Deletion & re-index reconciliation — deleted/modified photos keep stale index rows forever. [22 #23]

## Explicitly rejected — do not do

- GNN / graph embeddings over the entity graph — a few hundred nodes, one edge type, nothing for a GNN to
  learn that SQL doesn't already give. [22 #40]
- On-device LLM for query understanding — wrong latency trade for search. [22 #39]
