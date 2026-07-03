# 02 — Media Ingestion Pipeline

> How Vanta discovers, classifies, and registers every media file on the device.

---

## Overview

The ingestion pipeline is the first step in Vanta's lifecycle. It scans the Android **MediaStore** (the system index of all media), classifies files by type, and batches them into SQLite. No embeddings are generated at this stage — only metadata registration.



---

## Step-by-Step Flow

### 1. MediaStore Query

Android maintains a system-wide index of all media in `MediaStore`. Vanta queries four collections:



**Per-collection projection:**

| Collection | Queried Columns |
|------------|-----------------|
| Images | `_ID`, `DATA`, `DISPLAY_NAME`, `MIME_TYPE`, `SIZE`, `DATE_MODIFIED`, `WIDTH`, `HEIGHT` |
| Videos | Above + `DURATION` |
| Audio | `_ID`, `DATA`, `DISPLAY_NAME`, `MIME_TYPE`, `SIZE`, `DATE_MODIFIED`, `DURATION` |
| Documents | `_ID`, `DATA`, `DISPLAY_NAME`, `MIME_TYPE`, `SIZE`, `DATE_MODIFIED` |

> **Note:** `DATA` (absolute path) is deprecated since Android 11+ (scoped storage). It is used opportunistically when available, but the code falls back to MIME-based classification.

---

## 2. File Classification

Each row is classified into a `FileType` using **extension-first, MIME-fallback** logic:



**Supported extensions:**

| Type | Extensions |
|------|-----------|
| Image | `.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.heic`, `.heif`, `.tiff`, `.dng`, `.arw`, `.cr2`, `.cr3`, `.nef`, `.raf`, `.orf`, `.rw2` |
| Video | `.mp4`, `.webm`, `.mov`, `.avi`, `.mkv`, `.prores`, `.r3d` |
| Audio | `.mp3`, `.flac`, `.wav`, `.aac`, `.m4a`, `.ogg`, `.alac` |
| Document | `.pdf`, `.docx`, `.txt`, `.md`, `.csv`, `.pptx`, `.xlsx`, `.xls`, `.odt`, `.ods`, `.rtf` |

---

## 3. FileMeta Construction

Each accepted row becomes a `FileMeta` data class:

```kotlin
data class FileMeta(
    val mediaId: Long,           // MediaStore _ID (stable identifier)
    val contentUri: String,      // content://media/... (primary access handle)
    val absPath: String?,        // Legacy absolute path (optional)
    val displayName: String,     // Original filename
    val fileDtype: FileType,     // image | video | audio | document
    val mimeType: String,        // Full MIME type
    val sizeBytes: Long,
    val mtimeUnix: Long,         // Modified time (seconds)
    val lastIndexedAt: Long,     // 0 = not yet indexed
    val widthPx: Int,
    val heightPx: Int,
    val durationMs: Long?,       // null for images
    val status: String,          // "pending"
    val retryCount: Int          // 0
)
```



---

## 4. Batching & JNI Handoff

Files are collected in batches of 200 and passed to the C++ layer via JNI:



**JNI Reflection for FileMeta:**

The C++ side uses `FindClass`, `GetFieldID`, and `GetObjectField` to read each `FileMeta` field dynamically. Field IDs are fetched once per call (not cached across calls, since JNIEnv is thread-local).

```cpp
// C++ side: startStoring
jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
jfieldID absPathField = env->GetFieldID(fileMetaClass, "absPath", "Ljava/lang/String;");
jfieldID sizeBytesField = env->GetFieldID(fileMetaClass, "sizeBytes", "J");
// ... etc for all fields
```

---

## 5. SQLite Storage



**Key constraints:**

- `abs_path TEXT NOT NULL UNIQUE` — prevents duplicate entries across re-scans
- `status CHECK(status IN ('pending','indexed','failed','skipped'))` — state machine
- `filetype CHECK(filetype IN ('picture','document','audio','video'))` — validated types

---

## State Machine



---

## Re-Scan Behavior

When the user triggers a re-scan:

1. **New files** → inserted as `pending`
2. **Existing files** → skipped due to `UNIQUE` constraint on `abs_path`
3. **Deleted files** → remain in DB (orphan rows) — future cleanup job should handle this

---

## Performance Characteristics

| Metric | Typical Value |
|--------|--------------|
| Scan speed | ~10,000 files/sec (MediaStore cursor) |
| Batch size | 200 files per JNI call |
| JNI overhead | ~1-2 ms per batch (reflection cost) |
| SQLite insert | ~1-5 ms per batch (with WAL mode) |
| Total scan time | < 1 sec for 10,000 files |

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Null cursor | Collection treated as empty, scan continues |
| Missing columns | Required columns (`_ID`, `MIME_TYPE`, `SIZE`, `DATE_MODIFIED`) abort collection; optional columns default to 0 |
| Invalid MIME | Row skipped (not inserted) |
| JNI class not found | `startStoring` returns `JNI_FALSE` |
| SQLite busy | Retry with exponential backoff (handled by SQLite WAL) |
