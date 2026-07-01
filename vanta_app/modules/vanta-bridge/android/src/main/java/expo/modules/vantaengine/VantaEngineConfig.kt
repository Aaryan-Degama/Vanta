/**
 * Centralized configuration for the Vanta native engine.
 *
 * This object is responsible for resolving the directory where ONNX models and
 * tokenizer assets are stored. It avoids hard-coding the application package
 * path in C++ and makes the engine robust against different build variants or
 * application IDs.
 */
package expo.modules.vantaengine

import android.content.Context

object VantaEngineConfig {

    /**
     * Subdirectory inside [Context.getFilesDir] where Vanta models live.
     *
     * The app copies ONNX and tokenizer files from assets into this directory
     * at runtime so they can be mmap'd by ONNX Runtime.
     */
    private const val MODELS_DIR_NAME = "VantaModels"

    /**
     * Returns the absolute path to the directory containing Vanta models.
     *
     * The app copies ONNX and tokenizer files from assets into
     * `<filesDir>/VantaModels` at runtime so they can be mmap'd by ONNX Runtime.
     *
     * @param context Any application context.
     * @return Absolute path ending with `/VantaModels`.
     */
    fun getModelsDirectory(context: Context): String {
        return java.io.File(context.filesDir, MODELS_DIR_NAME).absolutePath
    }

    /**
     * Returns the absolute path to the SQLite database file.
     *
     * Using [Context.getDatabasePath] keeps the database in Android's standard
     * database location.
     *
     * @param context Any application context.
     * @return Absolute path to `vanta.db`.
     */
    fun getDatabasePath(context: Context): String {
        return context.getDatabasePath("vanta.db").absolutePath
    }
}
