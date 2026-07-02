package expo.modules.vantaengine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat

class IndexingService : Service() {
    companion object {
        const val CHANNEL_ID = "vanta_indexing_channel"
        const val NOTIFICATION_ID = 1001
        const val ACTION_START = "ACTION_START"
        const val ACTION_STOP = "ACTION_STOP"
    }

    private external fun setModelsDirNative(modelsDir: String)
    private external fun setCropsDirNative(cropsDir: String)
    private external fun generateEmbeddingsNative(dbPath: String): Boolean

    private var indexingThread: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_START) {
            val dbPath = intent.getStringExtra("dbPath") ?: return START_NOT_STICKY

            createNotificationChannel()
            val notification: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("Vanta Indexing")
                .setContentText("Processing photos in the background...")
                .setSmallIcon(android.R.drawable.ic_menu_gallery)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build()

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                try {
                    startForeground(NOTIFICATION_ID, notification, 1)
                } catch (e: Exception) {
                    startForeground(NOTIFICATION_ID, notification)
                }
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }

            if (indexingThread == null || !indexingThread!!.isAlive) {
                indexingThread = Thread {
                    try {
                        extractAssetsIfNeeded(this)
                        val modelsDir = VantaEngineConfig.getModelsDirectory(this)
                        val cropsDir = VantaEngineConfig.getCropsDirectory(this)
                        setModelsDirNative(modelsDir)
                        setCropsDirNative(cropsDir)
                        generateEmbeddingsNative(dbPath)
                    } catch (e: Exception) {
                        e.printStackTrace()
                    } finally {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                            stopForeground(STOP_FOREGROUND_REMOVE)
                        } else {
                            @Suppress("DEPRECATION")
                            stopForeground(true)
                        }
                        stopSelf()
                    }
                }
                indexingThread?.start()
            }
        } else if (intent?.action == ACTION_STOP) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                stopForeground(STOP_FOREGROUND_REMOVE)
            } else {
                @Suppress("DEPRECATION")
                stopForeground(true)
            }
            stopSelf()
        }
        return START_STICKY
    }

    /**
     * Copies bundled model assets from the APK to the app's files directory.
     */
    private fun extractAssetsIfNeeded(context: android.content.Context) {
        val modelsDir = java.io.File(VantaEngineConfig.getModelsDirectory(context))
        if (!modelsDir.exists()) modelsDir.mkdirs()

        val filesToCopy = listOf(
            "clip_image_fp16.onnx",
            "clip_text_fp16.onnx",
            "vocab.json",
            "merges.txt",
            "det_10g.onnx",
            "w600k_r50.onnx"
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
                Log.w("VantaEngine", "Failed to extract asset $fileName", e)
            }
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "Indexing Service Channel",
                NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(serviceChannel)
        }
    }
}
