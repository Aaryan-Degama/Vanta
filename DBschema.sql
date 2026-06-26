-- Vanta DB Schema
-- SQLite + sqlite-vec

-- FILES
-- Central registry for all media files on device.
CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY,
    abs_path        TEXT    NOT NULL UNIQUE,
    filetype        TEXT    NOT NULL CHECK(filetype IN ('picture','document','audio','video')),
    mime_type       TEXT,
    size_bytes      INTEGER,
    mtime_unix      INTEGER NOT NULL,
    last_indexed_at INTEGER,
    width_px        INTEGER,
    height_px       INTEGER,
    duration_ms     INTEGER,
    face_count      INTEGER NOT NULL DEFAULT 0,  -- total detected faces, used for exclusive entity queries
    status          TEXT    NOT NULL DEFAULT 'pending'
                    CHECK(status IN ('pending','indexed','failed','skipped')),
    retry_count     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_files_filetype ON files(filetype);
CREATE INDEX IF NOT EXISTS idx_files_mtime    ON files(mtime_unix);
CREATE INDEX IF NOT EXISTS idx_files_status     ON files(status);
CREATE INDEX IF NOT EXISTS idx_files_face_count ON files(face_count);
-- NOTE: abs_path UNIQUE already creates an implicit index, no explicit one needed

-- CLIP EMBEDDINGS
-- One 512-dim embedding per file (images + video keyframes).

CREATE VIRTUAL TABLE IF NOT EXISTS clip_vec USING vec0(
    file_id     INTEGER PRIMARY KEY,
    embedding   FLOAT[512]
);

-- PERSON ENTITIES
-- One row per discovered person cluster.

CREATE TABLE IF NOT EXISTS entities (
    entity_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    entity_type     TEXT    NOT NULL CHECK(entity_type IN ('person')),
    display_name    TEXT,
    confidence      REAL    NOT NULL DEFAULT 1.0,
    sample_count    INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_entities_type ON entities(entity_type);

-- PERSON CENTROIDS
-- One 512-dim ArcFace centroid per entity (buffalo_sc / w600k_mbf).
-- Updated incrementally: new_c = (old_c * n + new_emb) / (n + 1)
-- NOT a file embedding — this is the averaged cluster center.

CREATE VIRTUAL TABLE IF NOT EXISTS person_centroids USING vec0(
    entity_id   INTEGER PRIMARY KEY,
    embedding   FLOAT[512]
);

-- FACE DETECTIONS
-- One row per detected face per file (det_500m output).
-- entity_id is NULL until the face is assigned to a cluster.

CREATE TABLE IF NOT EXISTS face_detections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id     INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    entity_id   INTEGER REFERENCES entities(entity_id) ON DELETE SET NULL,
    bbox_x      INTEGER NOT NULL,
    bbox_y      INTEGER NOT NULL,
    bbox_w      INTEGER NOT NULL,
    bbox_h      INTEGER NOT NULL,
    det_score   REAL    NOT NULL,
    created_at  INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_face_det_file   ON face_detections(file_id);
CREATE INDEX IF NOT EXISTS idx_face_det_entity ON face_detections(entity_id);

-- FACE EMBEDDINGS
-- One 512-dim ArcFace embedding per detection (w600k_mbf output).
-- Stored fp32 — realistic device scale (~30-60MB) makes this fine.
-- Enables ANN face search and future re-clustering without re-inference.

CREATE VIRTUAL TABLE IF NOT EXISTS face_vec USING vec0(
    detection_id    INTEGER PRIMARY KEY,
    embedding       FLOAT[512]
);

-- ENTITY <-> FILE MEMBERSHIP
-- Links a person entity to every file they appear in.
-- score = similarity of best matching face in that file to centroid.

CREATE TABLE IF NOT EXISTS entity_memberships (
    entity_id   INTEGER NOT NULL,
    file_id     INTEGER NOT NULL,
    score       REAL    DEFAULT 1.0,
    created_at  INTEGER NOT NULL,
    PRIMARY KEY (entity_id, file_id),
    FOREIGN KEY (entity_id) REFERENCES entities(entity_id) ON DELETE CASCADE,
    FOREIGN KEY (file_id)   REFERENCES files(id)           ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_memberships_entity ON entity_memberships(entity_id);
CREATE INDEX IF NOT EXISTS idx_memberships_file   ON entity_memberships(file_id);

-- DOCUMENT EMBEDDINGS
-- One 384-dim bge-small embedding per document chunk.
-- chunk_index allows multi-chunk docs (long PDFs etc).

CREATE TABLE IF NOT EXISTS document_chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id     INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    chunk_index INTEGER NOT NULL DEFAULT 0,
    chunk_text  TEXT    NOT NULL,
    created_at  INTEGER NOT NULL,
    UNIQUE(file_id, chunk_index)
);

CREATE INDEX IF NOT EXISTS idx_doc_chunks_file ON document_chunks(file_id);

CREATE VIRTUAL TABLE IF NOT EXISTS document_vec USING vec0(
    chunk_id    INTEGER PRIMARY KEY,
    embedding   FLOAT[384]
);