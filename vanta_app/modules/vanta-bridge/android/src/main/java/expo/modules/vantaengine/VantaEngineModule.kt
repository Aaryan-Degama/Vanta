/**
 * Expo native module that bridges JavaScript to the Vanta C++ engine.
 *
 * This module exposes AsyncFunctions for scanning device media, generating
 * embeddings, searching, and querying face clusters. It also initializes the
 * C++ layer with the correct model directory at module creation time.
 */
package expo.modules.vantaengine

// Android Context and logging used throughout the module.
import android.content.Context
import android.util.Log

// Permission APIs used for pre-scan logging.
import android.Manifest
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat

// Expo Modules API base classes.
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import expo.modules.kotlin.Promise

class VantaEngineModule : Module() {

    companion object {
        init {
            // Load the native C++ shared library (libvanta.so).
            // This must happen before any JNI function is invoked.
            System.loadLibrary("vanta")
        }
    }

    //region JNI declarations

    /**
     * One-time initialization that tells the C++ engine where models live.
     *
     * Must be called before any function that loads ONNX models or tokenizers.
     */
    private external fun setModelsDirNative(modelsDir: String)

    /**
     * Receives file metadata from the MediaStore scan and inserts it into SQLite.
     */
    private external fun startStoring(dbPath: String, files: Array<FileMeta>): Boolean

    /**
     * Reads stored file records from SQLite and returns them as HashMaps.
     */
    private external fun getStoredFilesNative(dbPath: String): Array<HashMap<String, Any>>

    /**
     * Triggers CLIP + face embedding generation in C++.
     */
    private external fun generateEmbeddingsNative(dbPath: String): Boolean

    /**
     * Returns the current indexing progress as a JSON string.
     */
    private external fun getIndexProgressNative(): String

    /**
     * Signals the indexing loop to pause.
     */
    private external fun pauseEmbeddingsNative()

    /**
     * Returns database statistics as a JSON string.
     */
    private external fun getDatabaseStatsNative(dbPath: String): String

    /**
     * Searches indexed images using a natural-language query.
     */
    private external fun searchImagesNative(dbPath: String, query: String): String

    /**
     * Returns the top face entities as a JSON string.
     */
    private external fun getTopEntitiesNative(dbPath: String): String

    /**
     * Returns the best thumbnail crop for a face entity as JSON.
     */
    private external fun getBestFaceCropNative(dbPath: String, entityId: Long): String

    /**
     * Renames a face entity.
     */
    private external fun setEntityNameNative(dbPath: String, entityId: Long, name: String): Boolean

    /**
     * Returns co-occurring people for a face entity as JSON.
     */
    private external fun getEntityNeighborsNative(dbPath: String, entityId: Long): String

    /**
     * Returns files associated with a face entity as JSON.
     */
    private external fun getEntityFilesNative(dbPath: String, entityId: Long): String

    //endregion

    /**
     * Copies bundled model assets from the APK to the app's files directory.
     *
     * ONNX Runtime needs the models to be regular files on disk, so we extract
     * them once on first use.
     */
    private fun extractAssetsIfNeeded(context: Context) {
        val modelsDir = java.io.File(VantaEngineConfig.getModelsDirectory(context))
        if (!modelsDir.exists()) modelsDir.mkdirs()

        val filesToCopy = listOf(
            "clip_image_fp16.onnx",
            "clip_text_fp16.onnx",
            "vocab.json",
            "merges.txt",
            "det_500m.onnx",
            "w600k_mbf.onnx"
        )

        for (fileName in filesToCopy) {
            val file = java.io.File(modelsDir, fileName)
            if (file.exists()) continue

            try {
                context.assets.open("VantaModels/$fileName").use { input ->
                    java.io.FileOutputStream(file).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                // Asset may be missing in development; the C++ layer will log
                // a clearer error when it tries to load the model.
                Log.w("VantaEngine", "Failed to extract asset $fileName", e)
            }
        }
    }

    override fun definition() = ModuleDefinition {
        // Name exposed to JavaScript as `VantaEngine`.
        Name("VantaEngine")

        /**
         * Lifecycle hook called once when the module is created.
         *
         * We extract model assets and tell the C++ engine where they live so
         * model loading never depends on a hard-coded package path.
         */
        OnCreate {
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            extractAssetsIfNeeded(context)
            setModelsDirNative(VantaEngineConfig.getModelsDirectory(context))
        }

        // Main entry point for scanning and storing files.
        AsyncFunction("startStoring") {
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            logMediaPermissions(context)

            // Collect every file discovered during the MediaStore scan.
            val allFiles = mutableListOf<FileMeta>()
            scanPhone(context) { batch ->
                allFiles.addAll(batch)
            }

            logScanSummary(allFiles)

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            java.io.File(dbPath).parentFile?.mkdirs()

            // Hand collected metadata to the C++ engine.
            val success = startStoring(dbPath, allFiles.toTypedArray())

            mapOf(
                "success" to success,
                "databasePath" to dbPath,
                "databaseExists" to java.io.File(dbPath).exists(),
                "databaseSizeBytes" to if (java.io.File(dbPath).exists())
                    java.io.File(dbPath).length()
                else
                    0L,
                "scannedFileCount" to allFiles.size,
                "stats" to allFiles.groupingBy { it.fileDtype }.eachCount().toString()
            )
        }

        // Debug helper that reads records back from SQLite.
        AsyncFunction("getStoredFiles") {
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) {
                return@AsyncFunction emptyList<Map<String, Any>>()
            }

            val nativeResults = getStoredFilesNative(dbPath)
            nativeResults.toList()
        }

        // Starts the foreground-service indexer.
        AsyncFunction("generateEmbeddings") { promise: Promise ->
            val context = appContext.reactContext
            if (context == null) {
                promise.reject("ERR", "Android context unavailable", null)
                return@AsyncFunction
            }

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) {
                promise.resolve(false)
                return@AsyncFunction
            }

            val serviceIntent = android.content.Intent(context, IndexingService::class.java).apply {
                action = IndexingService.ACTION_START
                putExtra("dbPath", dbPath)
                putExtra("modelsDir", VantaEngineConfig.getModelsDirectory(context))
            }

            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent)
            } else {
                context.startService(serviceIntent)
            }

            promise.resolve(true)
        }

        AsyncFunction("getIndexProgress") {
            getIndexProgressNative()
        }

        AsyncFunction("pauseEmbeddings") {
            pauseEmbeddingsNative()
        }

        AsyncFunction("getDatabaseStats") {
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) {
                return@AsyncFunction "{}"
            }

            getDatabaseStatsNative(dbPath)
        }

        AsyncFunction("searchImages") { query: String, promise: Promise ->
            val context = appContext.reactContext
            if (context == null) {
                promise.reject("ERR", "Android context unavailable", null)
                return@AsyncFunction
            }

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) {
                promise.resolve("[]")
                return@AsyncFunction
            }

            Thread {
                try {
                    // Ensure models are extracted in case search is called
                    // before indexing has ever run.
                    extractAssetsIfNeeded(context)
                    setModelsDirNative(VantaEngineConfig.getModelsDirectory(context))
                    val result = searchImagesNative(dbPath, query)
                    promise.resolve(result)
                } catch (e: Exception) {
                    promise.reject("ERR", e.message, e)
                }
            }.start()
        }

        AsyncFunction("getTopEntities") {
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) return@AsyncFunction "[]"

            getTopEntitiesNative(dbPath)
        }

        AsyncFunction("getBestFaceCrop") { entityId: Double ->
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) return@AsyncFunction "{}"

            getBestFaceCropNative(dbPath, entityId.toLong())
        }

        AsyncFunction("setEntityName") { entityId: Double, name: String ->
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) return@AsyncFunction false

            setEntityNameNative(dbPath, entityId.toLong(), name)
        }

        AsyncFunction("getEntityNeighbors") { entityId: Double ->
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) return@AsyncFunction "[]"

            getEntityNeighborsNative(dbPath, entityId.toLong())
        }

        AsyncFunction("getEntityFiles") { entityId: Double ->
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbPath = VantaEngineConfig.getDatabasePath(context)
            if (!java.io.File(dbPath).exists()) return@AsyncFunction "[]"

            getEntityFilesNative(dbPath, entityId.toLong())
        }
    }

    /**
     * Logs whether the media permissions required for scanning are granted.
     */
    private fun logMediaPermissions(context: Context) {
        val permissions = listOf(
            Manifest.permission.READ_MEDIA_IMAGES,
            Manifest.permission.READ_MEDIA_VIDEO,
            Manifest.permission.READ_MEDIA_AUDIO
        )

        for (permission in permissions) {
            val granted = ContextCompat.checkSelfPermission(
                context,
                permission
            ) == PackageManager.PERMISSION_GRANTED
            Log.d("VANTA_SCAN", "$permission = $granted")
        }
    }

    /**
     * Logs a concise summary of the MediaStore scan.
     */
    private fun logScanSummary(files: List<FileMeta>) {
        val totalBytes = files.sumOf { it.sizeBytes }
        val totalGB = totalBytes.toDouble() / (1024.0 * 1024.0 * 1024.0)
        val stats = files.groupingBy { it.fileDtype }.eachCount()

        Log.d("VANTA_SCAN", "TOTAL SIZE = %.2f GB".format(totalGB))
        Log.d("VANTA_SCAN", "TOTAL FILES = ${files.size}")
        Log.d("VANTA_SCAN", "BREAKDOWN = $stats")
    }
}
