-- Schema version 1.
--
-- Applied once when a fresh database is opened. Later revisions append a new
-- file and bump the target version in store.go; existing tables are never
-- rewritten in place.

CREATE TABLE devices (
    id          TEXT PRIMARY KEY,
    name        TEXT,
    first_seen  INTEGER NOT NULL,
    last_seen   INTEGER NOT NULL
) STRICT;

CREATE TABLE memos (
    id           TEXT PRIMARY KEY,
    device_id    TEXT NOT NULL,
    recorded_at  INTEGER NOT NULL,
    uploaded_at  INTEGER NOT NULL,
    duration_ms  INTEGER NOT NULL,
    frame_count  INTEGER NOT NULL,
    size_bytes   INTEGER NOT NULL,
    codec        TEXT NOT NULL,
    status       TEXT NOT NULL,
    transcript   TEXT,
    lang         TEXT,
    asr_provider TEXT,
    asr_model    TEXT,
    asr_trace_id TEXT,
    edited_at    INTEGER,
    error        TEXT,
    audio_path   TEXT NOT NULL,
    clock_skew   INTEGER NOT NULL DEFAULT 0,
    deleted_at   INTEGER
) STRICT;

CREATE INDEX memos_order ON memos (recorded_at DESC, id DESC)
    WHERE deleted_at IS NULL;

CREATE TABLE jobs (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    memo_id   TEXT NOT NULL UNIQUE,
    attempts  INTEGER NOT NULL DEFAULT 0,
    next_at   INTEGER NOT NULL
) STRICT;
