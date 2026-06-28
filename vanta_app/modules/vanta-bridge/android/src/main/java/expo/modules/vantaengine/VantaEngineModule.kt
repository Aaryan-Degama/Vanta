// Package containing the Expo native module implementation.
// Functions defined here can be called directly from React Native / Expo.
package expo.modules.vantaengine

// Android permission APIs used for permission checking logs.
import android.Manifest
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat

// Expo Modules API base classes.
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

class VantaEngineModule : Module() {

    companion object {
        init {
            // Load the native C++ shared library (libvanta.so).
            // This must be loaded before any JNI/native function is called.
            System.loadLibrary("vanta")
        }
    }

    // Native JNI function implemented in C++.
    // Receives the database path and all scanned files,
    // then stores them inside SQLite.
    private external fun startStoring(
        dbPath: String,
        files: Array<FileMeta>
    ): Boolean

    // Native JNI function implemented in C++.
    // Reads stored file records from SQLite and returns them
    // as an array of HashMaps.
    private external fun getStoredFilesNative(
        dbPath: String
    ): Array<HashMap<String, Any>>

    // Triggers CLIP embedding generation in C++
    private external fun generateEmbeddingsNative(
        dbPath: String
    ): Boolean

    // Gets the current progress [processed, total]
    private external fun getIndexProgressNative(): String

    // Pauses embedding generation
    private external fun pauseEmbeddingsNative()

    private external fun getDatabaseStatsNative(
        dbPath: String
    ): String

    private external fun searchImagesNative(
        dbPath: String,
        query: String
    ): String

    private fun extractAssetsIfNeeded(context: android.content.Context) {
        val modelsDir = java.io.File(context.filesDir, "VantaModels")
        if (!modelsDir.exists()) {
            modelsDir.mkdirs()
        }

        val imageModel = java.io.File(modelsDir, "clip_image_fp16.onnx")
        if (!imageModel.exists()) {
            try {
                context.assets.open("VantaModels/clip_image_fp16.onnx").use { input ->
                    java.io.FileOutputStream(imageModel).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }

        val textModel = java.io.File(modelsDir, "clip_text_fp16.onnx")
        if (!textModel.exists()) {
            try {
                context.assets.open("VantaModels/clip_text_fp16.onnx").use { input ->
                    java.io.FileOutputStream(textModel).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }

        val vocabFile = java.io.File(modelsDir, "vocab.json")
        if (!vocabFile.exists()) {
            try {
                context.assets.open("VantaModels/vocab.json").use { input ->
                    java.io.FileOutputStream(vocabFile).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }

        val mergesFile = java.io.File(modelsDir, "merges.txt")
        if (!mergesFile.exists()) {
            try {
                context.assets.open("VantaModels/merges.txt").use { input ->
                    java.io.FileOutputStream(mergesFile).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    override fun definition() = ModuleDefinition {

        // Name exposed to JavaScript.
        // JS can access this module as "VantaEngine".
        Name("VantaEngine")

        // Main entry point for scanning and storing files.
        AsyncFunction("startStoring") {

            // Android context required for MediaStore access,
            // database creation, permissions, etc.
            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            // Log media permissions so we can quickly verify
            // whether Android granted access before scanning.

            android.util.Log.d(
                "VANTA_SCAN",
                "READ_MEDIA_IMAGES = ${
                    androidx.core.content.ContextCompat.checkSelfPermission(
                        context,
                        android.Manifest.permission.READ_MEDIA_IMAGES
                    ) == android.content.pm.PackageManager.PERMISSION_GRANTED
                }"
            )

            android.util.Log.d(
                "VANTA_SCAN",
                "READ_MEDIA_VIDEO = ${
                    androidx.core.content.ContextCompat.checkSelfPermission(
                        context,
                        android.Manifest.permission.READ_MEDIA_VIDEO
                    ) == android.content.pm.PackageManager.PERMISSION_GRANTED
                }"
            )

            android.util.Log.d(
                "VANTA_SCAN",
                "READ_MEDIA_AUDIO = ${
                    androidx.core.content.ContextCompat.checkSelfPermission(
                        context,
                        android.Manifest.permission.READ_MEDIA_AUDIO
                    ) == android.content.pm.PackageManager.PERMISSION_GRANTED
                }"
            )

            // Stores every file discovered during the MediaStore scan.
            val allFiles = mutableListOf<FileMeta>()

            // scanPhone() emits files in batches.
            // We merge all batches into a single collection.
            scanPhone(context) { batch ->
                allFiles.addAll(batch)
            }

            // Calculate total size of all discovered files.
            val totalBytes = allFiles.sumOf { it.sizeBytes }

            // Convert bytes into GB for easier reading.
            val totalGB = totalBytes.toDouble() /
                    (1024.0 * 1024.0 * 1024.0)

            android.util.Log.d(
                "VANTA_SCAN",
                "TOTAL SIZE = %.2f GB".format(totalGB)
            )

            // Total number of files found on the device.
            android.util.Log.d(
                "VANTA_SCAN",
                "TOTAL FILES = ${allFiles.size}"
            )

            // Count files grouped by type.
            // Example:
            // {IMAGE=5000, VIDEO=200, AUDIO=50}
            val stats = allFiles
                .groupingBy { it.fileDtype }
                .eachCount()

            android.util.Log.d(
                "VANTA_SCAN",
                "BREAKDOWN = $stats"
            )

            // Android-managed location where the SQLite database
            // will be stored.
            val dbFile = context.getDatabasePath("vanta.db")

            // Create database directory if it does not already exist.
            dbFile.parentFile?.mkdirs()

            // Hand all collected metadata to the native C++ layer.
            // The native code is responsible for creating/opening
            // SQLite and inserting rows.
            val success = startStoring(
                dbFile.absolutePath,
                allFiles.toTypedArray()
            )

            // Return useful information back to JavaScript.
            mapOf(
                "success" to success,
                "databasePath" to dbFile.absolutePath,
                "databaseExists" to dbFile.exists(),
                "databaseSizeBytes" to if (dbFile.exists())
                    dbFile.length()
                else
                    0L,
                "scannedFileCount" to allFiles.size,
                "stats" to stats.toString()
            )
        }

        // Debug helper that reads records back from SQLite via C++.
        // Useful for verifying that native insertion worked correctly.
        AsyncFunction("getStoredFiles") {

            val context = appContext.reactContext
                ?: throw IllegalStateException("Android context unavailable")

            val dbFile = context.getDatabasePath("vanta.db")

            // No database means nothing has been indexed yet.
            if (!dbFile.exists())
                return@AsyncFunction emptyList<Map<String, Any>>()

            // Delegate entirely to native C++ for reading.
            val nativeResults = getStoredFilesNative(dbFile.absolutePath)

            // Convert Array<HashMap> to List<Map> for React Native.
            nativeResults.toList()
        }

        AsyncFunction("generateEmbeddings") { promise: expo.modules.kotlin.Promise ->
            val context = appContext.reactContext
            if (context == null) {
                promise.reject("ERR", "Android context unavailable", null)
                return@AsyncFunction
            }

            val dbFile = context.getDatabasePath("vanta.db")

            if (!dbFile.exists()) {
                promise.resolve(false)
                return@AsyncFunction
            }

            Thread {
                try {
                    // Extract models from assets to internal storage so C++ can read them
                    extractAssetsIfNeeded(context)

                    // Run embedding generation natively
                    val success = generateEmbeddingsNative(dbFile.absolutePath)
                    promise.resolve(success)
                } catch (e: Exception) {
                    promise.reject("ERR", e.message, e)
                }
            }.start()
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

            val dbFile = context.getDatabasePath("vanta.db")

            if (!dbFile.exists()) {
                return@AsyncFunction "{}"
            }

            getDatabaseStatsNative(dbFile.absolutePath)
        }

        AsyncFunction("searchImages") { query: String, promise: expo.modules.kotlin.Promise ->
            val context = appContext.reactContext
            if (context == null) {
                promise.reject("ERR", "Android context unavailable", null)
                return@AsyncFunction
            }

            val dbFile = context.getDatabasePath("vanta.db")

            if (!dbFile.exists()) {
                promise.resolve("[]")
                return@AsyncFunction
            }

            Thread {
                try {
                    // Ensure model files are extracted (in case search is called before indexing)
                    extractAssetsIfNeeded(context)
                    val result = searchImagesNative(dbFile.absolutePath, query)
                    promise.resolve(result)
                } catch (e: Exception) {
                    promise.reject("ERR", e.message, e)
                }
            }.start()
        }
    }
}