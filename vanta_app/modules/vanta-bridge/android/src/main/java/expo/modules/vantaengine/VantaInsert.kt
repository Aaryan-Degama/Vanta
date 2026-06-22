// JNI endpoint
//  It is the Datatype we need to insert in the files db
data class FileMeta(
    val absPath: String,
    val fileDtype: String,
    val mimeType: String,

    val sizeBytes: Long,
    val mtimeUnix: Long,
    val lastIndexedAt: Long,

    val widthPx: Int,
    val heightPx: Int,

    val durationMs: Int,

    val status: String,
    val retryCount: Int
)