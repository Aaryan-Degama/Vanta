// MediaLoader.kt

package expo.modules.vantaengine

import android.content.Context
import android.net.Uri

object MediaLoader {

    fun readBytes(
        context: Context,
        contentUri: String
    ): ByteArray? {

        return try {

            context.contentResolver
                .openInputStream(Uri.parse(contentUri))
                ?.use { it.readBytes() }

        } catch (e: Exception) {
            null
        }
    }
}