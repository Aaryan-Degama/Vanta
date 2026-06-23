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
    }
}