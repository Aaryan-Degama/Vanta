CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY,-- auto incremental IDS
    abs_path        TEXT    NOT NULL UNIQUE,
    filetype        TEXT    NOT NULL CHECK(filetype IN ('picture','document','audio','video')),
    mime_type       TEXT,               -- informational only, not used for routing
    size_bytes      INTEGER,
    mtime_unix      INTEGER NOT NULL,   -- last modified
    last_indexed_at INTEGER,            -- last time we indexed it
    width_px        INTEGER,            -- (for images)
    height_px       INTEGER,            -- (for images)
    duration_ms     INTEGER,            -- (for video and audio file type)
    status          TEXT    NOT NULL DEFAULT 'pending'
                    CHECK(status IN ('pending','indexed','failed','skipped')),
    retry_count     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_files_filetype ON files(filetype);
CREATE INDEX IF NOT EXISTS idx_files_mtime    ON files(mtime_unix);
CREATE INDEX IF NOT EXISTS idx_files_status   ON files(status);
CREATE INDEX IF NOT EXISTS idx_files_path     ON files(abs_path);



-- CLIP embedding
-- vector DB
CREATE VIRTUAL TABLE IF NOT EXISTS clip_vec USING vec0(
    file_id     INTEGER PRIMARY KEY,
    embedding   FLOAT[512]
);


