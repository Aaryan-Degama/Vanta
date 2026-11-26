package expo.modules.vantaengine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
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
    private external fun getPendingDocumentsNative(dbPath: String): String
    private external fun saveDocumentTextNative(dbPath: String, fileId: Long, name: String, body: String): Boolean
    private external fun isPauseRequestedNative(): Boolean

    /**
     * Extracts and full-text indexes every pending document. Stops on a pause;
     * the rest stay pending for the next run.
     */
    private fun indexDocuments(dbPath: String) {
        while (true) {
            val batch = org.json.JSONArray(getPendingDocumentsNative(dbPath))
            if (batch.length() == 0) return
            for (i in 0 until batch.length()) {
                if (isPauseRequestedNative()) return
                val doc = batch.getJSONObject(i)
                val path = doc.getString("path")
                // display_name, not the path: a hidden DATA column leaves a
                // content:// URI as the path, whose last segment is the media id.
                val name = doc.optString("display_name").ifEmpty { java.io.File(path).name }
                // Indexed even with no text, so the file is still found by name.
                // A failed save would return the same batch forever, so stop.
                if (!saveDocumentTextNative(dbPath, doc.getLong("id"),
                        name, extractDocumentText(path))) return
            }
        }
    }

    private var indexingThread: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_START) {
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

            // Checked only after startForeground: a service started through
            // startForegroundService must go foreground before it may stop.
            val dbPath = intent.getStringExtra("dbPath")
            if (dbPath == null) {
                if (indexingThread?.isAlive != true) stopForegroundAndSelf()
                return START_NOT_STICKY
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
                        // Documents need no models, so they run even when the
                        // photo run failed. After the photo run, not before:
                        // that run's first act is resetting the status the JS
                        // poller watches.
                        // ponytail: not shown in the progress UI; text
                        // extraction is fast next to CLIP.
                        indexDocuments(dbPath)
                    } catch (e: Exception) {
                        e.printStackTrace()
                    } finally {
                        stopForegroundAndSelf()
                    }
                }
                indexingThread?.start()
            }
        } else if (intent?.action == ACTION_STOP) {
            stopForegroundAndSelf()
        } else {
            // Null intent (only sent when restarting a sticky service, which
            // this one is not; handled defensively) or an unknown action:
            // there is no dbPath to work with, so do not linger.
            if (indexingThread?.isAlive != true) stopForegroundAndSelf()
        }
        // Not sticky: a restart delivers a null intent without the dbPath, and
        // an app in the background may not re-enter the foreground on
        // Android 12+, so a recreated service could not resume indexing anyway.
        return START_NOT_STICKY
    }

    private fun stopForegroundAndSelf() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
        stopSelf()
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
            // Atomic temp-file-and-rename copy, defined in VantaEngineModule.kt.
            extractModelAsset(context, modelsDir, fileName)
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
