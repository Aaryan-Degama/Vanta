// Android_scan.kt

package expo.modules.vantaengine

import android.content.ContentUris
import android.content.Context
import android.provider.MediaStore
import android.net.Uri

/**
 Broad file categories understood by the indexing pipeline.

 These values cross the Kotlin/JNI boundary as part of [FileMeta], so changes
 should remain synchronized with the native representation.
 */
enum class FileType {
    image,
    video,
    audio,
    document
}

/**
 Supported filename extensions, normalized to lowercase and including the
 leading dot. Extension-based classification is preferred because some
 Android content providers return generic or incorrect MIME types.
 */
val validDtype = hashMapOf(

    // IMAGE
    ".jpg"   to FileType.image,
    ".jpeg"  to FileType.image,
    ".png"   to FileType.image,
    ".webp"  to FileType.image,
    ".bmp"   to FileType.image,
    ".heic"  to FileType.image,
    ".heif"  to FileType.image,
    ".tiff"  to FileType.image,
    ".tif"   to FileType.image,
    ".dng"   to FileType.image,
    ".arw"   to FileType.image,
    ".cr2"   to FileType.image,
    ".cr3"   to FileType.image,
    ".nef"   to FileType.image,
    ".raf"   to FileType.image,
    ".orf"   to FileType.image,
    ".rw2"   to FileType.image,

    // VIDEO
    ".mp4"    to FileType.video,
    ".webm"   to FileType.video,
    ".mov"    to FileType.video,
    ".avi"    to FileType.video,
    ".mkv"    to FileType.video,
    ".prores" to FileType.video,
    ".r3d"    to FileType.video,

    // DOCUMENT
    ".pdf"  to FileType.document,
    ".docx" to FileType.document,
    ".txt"  to FileType.document,
    ".md"   to FileType.document,
    ".csv"  to FileType.document,
    ".pptx" to FileType.document,
    ".xlsx" to FileType.document,
    ".xls"  to FileType.document,
    ".odt"  to FileType.document,
    ".ods"  to FileType.document,
    ".rtf"  to FileType.document,

    // AUDIO
    ".mp3"  to FileType.audio,
    ".flac" to FileType.audio,
    ".wav"  to FileType.audio,
    ".aac"  to FileType.audio,
    ".m4a"  to FileType.audio,
    ".ogg"  to FileType.audio,
    ".alac" to FileType.audio
)

/**
 MIME fallback used when a legacy absolute path is unavailable, which is
 common under Android 11+ scoped storage.

 This map intentionally contains only MIME types supported by the indexer.
 Unknown values are ignored rather than assigned to a potentially incorrect
 category.
 */
private val mimeToFileType = hashMapOf(

    // IMAGE
    "image/jpeg"       to FileType.image,
    "image/png"        to FileType.image,
    "image/webp"       to FileType.image,
    "image/bmp"        to FileType.image,
    "image/heic"       to FileType.image,
    "image/heif"       to FileType.image,
    "image/tiff"       to FileType.image,

    // VIDEO
    "video/mp4"        to FileType.video,
    "video/webm"       to FileType.video,
    "video/quicktime"  to FileType.video,
    "video/x-msvideo"  to FileType.video,
    "video/x-matroska" to FileType.video,

    // DOCUMENT
    "application/pdf"  to FileType.document,
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document"   to FileType.document,
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"         to FileType.document,
    "application/vnd.openxmlformats-officedocument.presentationml.presentation" to FileType.document,
    "application/vnd.ms-excel"                        to FileType.document,
    "application/vnd.oasis.opendocument.text"         to FileType.document,
    "application/vnd.oasis.opendocument.spreadsheet"  to FileType.document,
    "application/rtf"  to FileType.document,
    "text/plain"       to FileType.document,
    "text/markdown"    to FileType.document,
    "text/csv"         to FileType.document,

    // AUDIO
    "audio/mpeg"   to FileType.audio,
    "audio/flac"   to FileType.audio,
    "audio/wav"    to FileType.audio,
    "audio/aac"    to FileType.audio,
    "audio/mp4"    to FileType.audio,
    "audio/ogg"    to FileType.audio,
    "audio/x-alac" to FileType.audio
)

/**
 Returns the supported [FileType] represented by this filename/path.

 The final path segment is inspected case-insensitively. A missing or
 unsupported extension returns `null`.
 */
fun String.toFileType(): FileType? {
    val ext = ".${this.substringAfterLast('.', "").lowercase()}"
    return validDtype[ext]
}

/**
 Convenience check for callers that only need to know whether a path has a
 supported extension.
 */
fun String?.isValidMedia(): Boolean {
    if (isNullOrBlank()) return false
    return this.toFileType() != null
}

/**
 Classifies a MediaStore row using its path first and MIME type second.

 A present path with an unknown extension is not reclassified from MIME. This
 avoids accepting files whose declared MIME conflicts with their filename.
 MIME fallback is reserved for rows where scoped storage hides `DATA`.
 */
private fun inferFileType(path: String?, mime: String): FileType? =
    path?.toFileType() ?: mimeToFileType[mime]

/**
 Metadata passed from the Android MediaStore scanner to the native indexer.

 [contentUri] is the stable Android access handle and database identity.
 [absPath] is optional legacy metadata only; consumers must not use it to open
 files because scoped storage may hide it or make it stale.
   Timestamps from MediaStore are expressed in Unix seconds, while media
 duration is expressed in milliseconds.
 */
data class FileMeta(
    val mediaId: Long,
    val contentUri: String,
    val absPath: String?,
    val fileDtype: FileType,
    val mimeType: String,
    val sizeBytes: Long,
    val mtimeUnix: Long,
    val lastIndexedAt: Long,
    val widthPx: Int,
    val heightPx: Int,
    val durationMs: Long?,
    val status: String,
    val retryCount: Int
)

// Internal per-type scanners

/**
 Lazily scans image rows. The MediaStore cursor is opened when this sequence
 is consumed, not when the sequence object is created.
 */
private fun scanImages(context: Context): Sequence<FileMeta> = sequence {

    // DATA is requested for extension classification on devices that expose it.
    // Access is optional; inferFileType() handles scoped-storage null values.
    @Suppress("DEPRECATION")
    val projection = arrayOf(
        MediaStore.Images.Media._ID,
        MediaStore.Images.Media.DATA,
        MediaStore.Images.Media.MIME_TYPE,
        MediaStore.Images.Media.SIZE,
        MediaStore.Images.Media.DATE_MODIFIED,
        MediaStore.Images.Media.WIDTH,
        MediaStore.Images.Media.HEIGHT
    )

    yieldFromMediaStore(
        context    = context,
        contentUri = MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
        projection = projection,
        fileType   = FileType.image
    ) { cursor ->
        val widthIdx  = cursor.getColumnIndex(MediaStore.Images.Media.WIDTH)
        val heightIdx = cursor.getColumnIndex(MediaStore.Images.Media.HEIGHT)
        Extras(
            widthPx    = if (widthIdx  != -1) cursor.getInt(widthIdx)  else 0,
            heightPx   = if (heightIdx != -1) cursor.getInt(heightIdx) else 0,
            durationMs = null
        )
    }
}

/**
 Lazily scans video rows and includes dimensions and duration when MediaStore
 exposes those columns.
 */
private fun scanVideos(context: Context): Sequence<FileMeta> = sequence {

    @Suppress("DEPRECATION")
    val projection = arrayOf(
        MediaStore.Video.Media._ID,
        MediaStore.Video.Media.DATA,
        MediaStore.Video.Media.MIME_TYPE,
        MediaStore.Video.Media.SIZE,
        MediaStore.Video.Media.DATE_MODIFIED,
        MediaStore.Video.Media.WIDTH,
        MediaStore.Video.Media.HEIGHT,
        MediaStore.Video.Media.DURATION
    )

    yieldFromMediaStore(
        context    = context,
        contentUri = MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
        projection = projection,
        fileType   = FileType.video
    ) { cursor ->
        val widthIdx    = cursor.getColumnIndex(MediaStore.Video.Media.WIDTH)
        val heightIdx   = cursor.getColumnIndex(MediaStore.Video.Media.HEIGHT)
        val durationIdx = cursor.getColumnIndex(MediaStore.Video.Media.DURATION)
        Extras(
            widthPx    = if (widthIdx    != -1) cursor.getInt(widthIdx)     else 0,
            heightPx   = if (heightIdx   != -1) cursor.getInt(heightIdx)    else 0,
            durationMs = if (durationIdx != -1) cursor.getLong(durationIdx) else null
        )
    }
}

/**
 Lazily scans audio rows. Visual dimensions are deliberately represented as
 zero in [FileMeta], while an unavailable duration remains null.
 */
private fun scanAudio(context: Context): Sequence<FileMeta> = sequence {

    @Suppress("DEPRECATION")
    val projection = arrayOf(
        MediaStore.Audio.Media._ID,
        MediaStore.Audio.Media.DATA,
        MediaStore.Audio.Media.MIME_TYPE,
        MediaStore.Audio.Media.SIZE,
        MediaStore.Audio.Media.DATE_MODIFIED,
        MediaStore.Audio.Media.DURATION
    )

    yieldFromMediaStore(
        context    = context,
        contentUri = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
        projection = projection,
        fileType   = FileType.audio
    ) { cursor ->
        val durationIdx = cursor.getColumnIndex(MediaStore.Audio.Media.DURATION)
        Extras(
            widthPx    = 0,
            heightPx   = 0,
            durationMs = if (durationIdx != -1) cursor.getLong(durationIdx) else null
        )
    }
}

/**
 Lazily scans MediaStore's generic Files collection for supported documents.

 Unlike images, videos, and audio, Android has no dedicated document table.
 Classification therefore happens row by row using extension or MIME data.
 */
private fun scanDocuments(context: Context): Sequence<FileMeta> = sequence {

    // No MIME filter in query — extension + MIME fallback classifies inside the loop.
    // SQL pre-filter only strips zero-byte and no-MIME rows to cut cache noise.
    @Suppress("DEPRECATION")
    val projection = arrayOf(
        MediaStore.Files.FileColumns._ID,
        MediaStore.Files.FileColumns.DATA,
        MediaStore.Files.FileColumns.MIME_TYPE,
        MediaStore.Files.FileColumns.SIZE,
        MediaStore.Files.FileColumns.DATE_MODIFIED
    )

    val selection =
        "${MediaStore.Files.FileColumns.SIZE} > 0 AND " +
        "${MediaStore.Files.FileColumns.MIME_TYPE} IS NOT NULL AND " +
        "${MediaStore.Files.FileColumns.MIME_TYPE} != ''"

    yieldFromMediaStore(
        context    = context,
        contentUri = MediaStore.Files.getContentUri("external"),
        projection = projection,
        fileType   = FileType.document,
        selection  = selection
    ) { _ ->
        Extras(widthPx = 0, heightPx = 0, durationMs = null)
    }
}

// ─── Shared cursor logic ──────────────────────────────────────────────────────

private data class Extras(
    val widthPx: Int,
    val heightPx: Int,
    val durationMs: Long?
)

/**
 Shared cursor reader used by every category-specific scanner.

 The caller supplies the collection/projection and extracts category-specific
 fields through [extractExtras]. Common validation, type inference, URI
 construction, and [FileMeta] defaults stay centralized here.

 This is a [SequenceScope] extension so each accepted row can be yielded
 immediately. The cursor remains open only while the sequence is consumed and
 is always closed by [android.database.Cursor.use].
 */
private suspend fun SequenceScope<FileMeta>.yieldFromMediaStore(
    context: Context,
    contentUri: Uri,
    projection: Array<String>,
    fileType: FileType,
    selection: String? = null,
    selectionArgs: Array<String>? = null,
    extractExtras: (android.database.Cursor) -> Extras
) {
    // A null cursor means the provider could not serve this query. Treat the
    // collection as empty so one unavailable media category does not crash the
    // complete phone scan.
    val cursor = context.contentResolver.query(
        contentUri,
        projection,
        selection,
        selectionArgs,
        null
    ) ?: return

    cursor.use {

        val idIdx       = it.getColumnIndex(MediaStore.MediaColumns._ID)
        val mimeIdx     = it.getColumnIndex(MediaStore.MediaColumns.MIME_TYPE)
        val sizeIdx     = it.getColumnIndex(MediaStore.MediaColumns.SIZE)
        val modifiedIdx = it.getColumnIndex(MediaStore.MediaColumns.DATE_MODIFIED)

        @Suppress("DEPRECATION")
        val pathIdx = it.getColumnIndex(MediaStore.MediaColumns.DATA)

        // These columns are required to build a usable FileMeta. DATA remains
        // optional because it is deprecated and often hidden by scoped storage.
        if (listOf(idIdx, mimeIdx, sizeIdx, modifiedIdx).any { idx -> idx == -1 })
            return@use

        while (it.moveToNext()) {

            val mime      = it.getString(mimeIdx) ?: ""
            val sizeBytes = it.getLong(sizeIdx)

            if (sizeBytes <= 0 || mime.isBlank()) continue

            // DATA may be null on Android 11+, in which case MIME classification
            // preserves support without depending on direct filesystem access.
            val path = if (pathIdx != -1) it.getString(pathIdx) else null

            val inferredType = inferFileType(path, mime)

            // The generic Files collection may contain every media category.
            // Keep only rows belonging to the scanner currently being executed.
            if (inferredType != fileType) continue

            val mediaId = it.getLong(idIdx)
            val uri     = ContentUris.withAppendedId(contentUri, mediaId)
            val extras  = extractExtras(it)

            yield(
                FileMeta(
                    mediaId       = mediaId,
                    contentUri    = uri.toString(),
                    absPath       = path,
                    fileDtype     = fileType,
                    mimeType      = mime,
                    sizeBytes     = sizeBytes,
                    mtimeUnix     = it.getLong(modifiedIdx),
                    // New rows begin in the native indexer's pending state.
                    lastIndexedAt = 0L,
                    widthPx       = extras.widthPx,
                    heightPx      = extras.heightPx,
                    durationMs    = extras.durationMs,
                    status        = "pending",
                    retryCount    = 0
                )
            )
        }
    }
}

// Public API

/**
 Scans all supported MediaStore collections and emits metadata in bounded
 batches.

 Scanning and [onBatch] invocation are synchronous on the calling thread.
 Callers should therefore run this function on a background thread and should
 return promptly from [onBatch]. Files are emitted by category (images,
 videos, audio, then documents); no global sort order is guaranteed.

 The final callback may receive fewer than [batchSize] items. Empty scans do
 not invoke [onBatch].

 @param context context whose [android.content.ContentResolver] can query
 MediaStore and for which the required media permissions have been granted.
 @param batchSize maximum number of rows delivered per callback. Must be
 positive; callers are responsible for supplying a valid value.
 @param onBatch receives an immutable snapshot of each completed batch.
 */
fun scanPhone(
    context: Context,
    batchSize: Int = 200,
    onBatch: (List<FileMeta>) -> Unit
) {
    val allFiles = sequenceOf(
        scanImages(context),
        scanVideos(context),
        scanAudio(context),
        scanDocuments(context)
    ).flatten()

    val batch = mutableListOf<FileMeta>()

    for (file in allFiles) {
        batch.add(file)
        if (batch.size >= batchSize) {
            // Copy before clearing the reusable buffer so the receiver retains
            // an independent batch after this callback returns.
            onBatch(batch.toList())
            batch.clear()
        }
    }

    // Flush the remainder that did not fill a complete batch.
    if (batch.isNotEmpty()) {
        onBatch(batch.toList())
    }
}
