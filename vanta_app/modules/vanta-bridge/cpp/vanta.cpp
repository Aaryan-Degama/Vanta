#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <exception>
#include <filesystem>
#include <system_error>
#include <utility>
#include <android/log.h>

#include "Config.hpp"
#include "json_utils.hpp"
#include "DBoperations.hpp"
#include "clip_db.hpp"
#include "graph_db.hpp"
#include "query_engine.hpp"
#include "CLIP_model.hpp"
#include "CLIP_tokenizer.hpp"
#include "face_DB.hpp"
#include "ner.hpp"

static CLIPTokenizer* g_tokenizer = nullptr;
static vanta::ner::NERModel* g_ner_model = nullptr;
static std::atomic<int64_t> g_owner_entity_id(-1);

// Guards all global model/session state so indexing and search cannot race
// while creating or loading sessions.
static std::mutex g_session_mutex;

#define LOG_TAG "VantaEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative(
    JNIEnv* env,
    jobject /* this */,
    jstring modelsDir) {

    if (modelsDir == nullptr) return;

    const char* models_dir_cstr = env->GetStringUTFChars(modelsDir, nullptr);
    VantaConfig::instance().set_models_dir(std::string(models_dir_cstr));
    env->ReleaseStringUTFChars(modelsDir, models_dir_cstr);

    LOGI("Models directory set to: %s", VantaConfig::instance().models_dir().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_setModelsDirNative(
    JNIEnv* env,
    jobject /* this */,
    jstring modelsDir) {

    Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative(env, nullptr, modelsDir);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setCropsDirNative(
    JNIEnv* env,
    jobject /* this */,
    jstring cropsDir) {

    if (cropsDir == nullptr) return;

    const char* crops_dir_cstr = env->GetStringUTFChars(cropsDir, nullptr);
    VantaConfig::instance().set_crops_dir(std::string(crops_dir_cstr));
    env->ReleaseStringUTFChars(cropsDir, crops_dir_cstr);

    LOGI("Crops directory set to: %s", VantaConfig::instance().crops_dir().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_setCropsDirNative(
    JNIEnv* env,
    jobject /* this */,
    jstring cropsDir) {

    Java_expo_modules_vantaengine_VantaEngineModule_setCropsDirNative(env, nullptr, cropsDir);
    return JNI_TRUE;
}

/**
 * startStoring
 * Receives an array of FileMeta objects from Java (Android MediaStore scan results)
 * and inserts them into the C++ SQLite database for indexing.
 * Ensures the 'image' type is mapped to 'picture' to satisfy SQLite CHECK constraints.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_startStoring(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jobjectArray files) {

    if (dbPath == nullptr || files == nullptr) return JNI_FALSE;

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    jsize count = env->GetArrayLength(files);
    LOGI("Received %d files to store", count);

    std::vector<file_meta> file_list;
    file_list.reserve(count);

    // FileMeta class from Android_scan.kt
    jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
    if (fileMetaClass == nullptr) return JNI_FALSE;

    // Field IDs matching the FileMeta data class in Android_scan.kt
    jfieldID contentUriField   = env->GetFieldID(fileMetaClass, "contentUri",    "Ljava/lang/String;");
    jfieldID absPathField      = env->GetFieldID(fileMetaClass, "absPath",       "Ljava/lang/String;");
    jfieldID displayNameField  = env->GetFieldID(fileMetaClass, "displayName",   "Ljava/lang/String;");
    jfieldID fileDtypeField    = env->GetFieldID(fileMetaClass, "fileDtype",     "Lexpo/modules/vantaengine/FileType;");
    jfieldID mimeTypeField     = env->GetFieldID(fileMetaClass, "mimeType",      "Ljava/lang/String;");
    jfieldID sizeBytesField    = env->GetFieldID(fileMetaClass, "sizeBytes",     "J");
    jfieldID mtimeUnixField    = env->GetFieldID(fileMetaClass, "mtimeUnix",     "J");
    jfieldID lastIndexedAtField = env->GetFieldID(fileMetaClass, "lastIndexedAt", "J");
    jfieldID widthPxField      = env->GetFieldID(fileMetaClass, "widthPx",       "I");
    jfieldID heightPxField     = env->GetFieldID(fileMetaClass, "heightPx",      "I");
    jfieldID durationMsField   = env->GetFieldID(fileMetaClass, "durationMs",    "Ljava/lang/Long;");
    jfieldID statusField       = env->GetFieldID(fileMetaClass, "status",        "Ljava/lang/String;");
    jfieldID retryCountField   = env->GetFieldID(fileMetaClass, "retryCount",    "I");

    // FileType enum — call .name() to get the string representation
    jclass fileTypeClass = env->FindClass("expo/modules/vantaengine/FileType");
    jmethodID nameMethod = env->GetMethodID(fileTypeClass, "name", "()Ljava/lang/String;");

    // java.lang.Long for nullable durationMs
    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID longValueMethod = env->GetMethodID(longClass, "longValue", "()J");

    for (jsize i = 0; i < count; ++i) {
        jobject fileObj = env->GetObjectArrayElement(files, i);
        if (fileObj == nullptr) continue;

        file_meta meta;

        // Prefer absPath (for OpenCV) over contentUri
        jstring jAbsPath = (jstring)env->GetObjectField(fileObj, absPathField);
        if (jAbsPath != nullptr) {
            const char* absPathCstr = env->GetStringUTFChars(jAbsPath, nullptr);
            meta.content_uri = absPathCstr;
            env->ReleaseStringUTFChars(jAbsPath, absPathCstr);
            env->DeleteLocalRef(jAbsPath);
        } else {
            jstring jContentUri = (jstring)env->GetObjectField(fileObj, contentUriField);
            if (jContentUri != nullptr) {
                const char* contentUriCstr = env->GetStringUTFChars(jContentUri, nullptr);
                meta.content_uri = contentUriCstr;
                env->ReleaseStringUTFChars(jContentUri, contentUriCstr);
                env->DeleteLocalRef(jContentUri);
            }
        }

        // displayName (String)
        jstring jDisplayName = (jstring)env->GetObjectField(fileObj, displayNameField);
        if (jDisplayName != nullptr) {
            const char* displayNameCstr = env->GetStringUTFChars(jDisplayName, nullptr);
            meta.display_name = displayNameCstr;
            env->ReleaseStringUTFChars(jDisplayName, displayNameCstr);
            env->DeleteLocalRef(jDisplayName);
        }

        // fileDtype (FileType enum → string via .name())
        jobject jFileType = env->GetObjectField(fileObj, fileDtypeField);
        if (jFileType != nullptr) {
            jstring jFileTypeName = (jstring)env->CallObjectMethod(jFileType, nameMethod);
            const char* fileTypeNameCstr = env->GetStringUTFChars(jFileTypeName, nullptr);
            
            // Map "image" to "picture" to satisfy SQLite CHECK constraint
            if (strcmp(fileTypeNameCstr, "image") == 0) {
                meta.file_type = "picture";
            } else {
                meta.file_type = fileTypeNameCstr;
            }
            
            env->ReleaseStringUTFChars(jFileTypeName, fileTypeNameCstr);
            env->DeleteLocalRef(jFileTypeName);
            env->DeleteLocalRef(jFileType);
        }

        // mimeType (String)
        jstring jMimeType = (jstring)env->GetObjectField(fileObj, mimeTypeField);
        if (jMimeType != nullptr) {
            const char* mimeTypeCstr = env->GetStringUTFChars(jMimeType, nullptr);
            meta.mime_type = mimeTypeCstr;
            env->ReleaseStringUTFChars(jMimeType, mimeTypeCstr);
            env->DeleteLocalRef(jMimeType);
        }

        // Primitive fields
        meta.size_bytes     = env->GetLongField(fileObj, sizeBytesField);
        meta.mtime_unix     = env->GetLongField(fileObj, mtimeUnixField);
        meta.last_indexed_at = env->GetLongField(fileObj, lastIndexedAtField);
        meta.width_px       = env->GetIntField(fileObj, widthPxField);
        meta.height_px      = env->GetIntField(fileObj, heightPxField);

        // durationMs (Long? — nullable boxed type)
        jobject jDurationMs = env->GetObjectField(fileObj, durationMsField);
        if (jDurationMs != nullptr) {
            meta.duration_ms = env->CallLongMethod(jDurationMs, longValueMethod);
            env->DeleteLocalRef(jDurationMs);
        } else {
            meta.duration_ms = 0;
        }

        // status (String)
        jstring jStatus = (jstring)env->GetObjectField(fileObj, statusField);
        if (jStatus != nullptr) {
            const char* statusCstr = env->GetStringUTFChars(jStatus, nullptr);
            meta.status = statusCstr;
            env->ReleaseStringUTFChars(jStatus, statusCstr);
            env->DeleteLocalRef(jStatus);
        }

        meta.retry_count = env->GetIntField(fileObj, retryCountField);

        file_list.push_back(meta);
        env->DeleteLocalRef(fileObj);
    }

    bool success = insert_files(file_list, db_path);
    return success ? JNI_TRUE : JNI_FALSE;
}

/**
 * getStoredFilesNative
 * Queries the SQLite database for stored files and returns them to Java
 * as an array of HashMaps containing uri, type, mime, and size.
 */
extern "C" JNIEXPORT jobjectArray JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getStoredFilesNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    if (dbPath == nullptr) return nullptr;

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    // Query files from C++ SQLite
    std::vector<file_meta> files = get_files(db_path, 100 );

    // Build a Java HashMap for each row and return as Object[]
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");
    jmethodID hashMapPut = env->GetMethodID(hashMapClass, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID longInit = env->GetMethodID(longClass, "<init>", "(J)V");

    jobjectArray resultArray = env->NewObjectArray(
        static_cast<jsize>(files.size()), hashMapClass, nullptr);

    for (size_t i = 0; i < files.size(); ++i) {
        const file_meta& f = files[i];

        jobject map = env->NewObject(hashMapClass, hashMapInit);

        // "uri" → content_uri
        jstring keyUri = env->NewStringUTF("uri");
        jstring valUri = env->NewStringUTF(f.content_uri.c_str());
        env->CallObjectMethod(map, hashMapPut, keyUri, valUri);
        env->DeleteLocalRef(keyUri);
        env->DeleteLocalRef(valUri);

        // "type" → file_type
        jstring keyType = env->NewStringUTF("type");
        jstring valType = env->NewStringUTF(f.file_type.c_str());
        env->CallObjectMethod(map, hashMapPut, keyType, valType);
        env->DeleteLocalRef(keyType);
        env->DeleteLocalRef(valType);

        // "mime" → mime_type
        jstring keyMime = env->NewStringUTF("mime");
        jstring valMime = env->NewStringUTF(f.mime_type.c_str());
        env->CallObjectMethod(map, hashMapPut, keyMime, valMime);
        env->DeleteLocalRef(keyMime);
        env->DeleteLocalRef(valMime);

        // "size" → size_bytes (as Long object)
        jstring keySize = env->NewStringUTF("size");
        jobject valSize = env->NewObject(longClass, longInit, f.size_bytes);
        env->CallObjectMethod(map, hashMapPut, keySize, valSize);
        env->DeleteLocalRef(keySize);
        env->DeleteLocalRef(valSize);

        env->SetObjectArrayElement(resultArray, static_cast<jsize>(i), map);
        env->DeleteLocalRef(map);
    }

    return resultArray;
}

static CLIP_session* g_clip_session = nullptr;
static Face_embedding* g_face_session = nullptr;
static std::atomic<int> g_index_processed_count(0);
static std::atomic<int> g_index_total_count(0);
// Written by the indexing thread and read by the progress poller; only touch
// these two strings through the helpers below.
static std::mutex g_index_state_mutex;
static std::string g_current_processing_file = "";
static std::string g_index_status = "idle";

static void set_index_status(const std::string& status) {
    std::lock_guard<std::mutex> lock(g_index_state_mutex);
    g_index_status = status;
}

static void set_current_processing_file(const std::string& file) {
    std::lock_guard<std::mutex> lock(g_index_state_mutex);
    g_current_processing_file = file;
}

static void get_index_state(std::string& status, std::string& file) {
    std::lock_guard<std::mutex> lock(g_index_state_mutex);
    status = g_index_status;
    file = g_current_processing_file;
}
static std::atomic<bool> g_pause_embedding(false);

// Held for the whole of an indexing run or a face re-scan: the two rewrite the
// same tables, so the second one to start is refused instead of interleaving.
static std::mutex g_job_mutex;

// Photos committed since the end-of-run passes last completed; guarded by
// g_job_mutex. Kept across runs, so the photos of a paused or failed run still
// get the passes when a later run finishes with nothing left to index.
// Starts at 1 so the first run of each process always runs the passes: a
// re-scan or a run killed before its passes finished leaves pending faces and
// an empty/stale graph that only those passes repair, and that state outlives
// the process while this counter does not.
// ponytail: in memory only, so the first run after every restart pays for the
// passes once. Persist a "passes needed" flag if that cost ever matters.
static int g_photos_since_passes = 1;

// For an indexing run that was refused because another job holds g_job_mutex.
// The status belongs to the job that holds the lock, so an active status is
// left alone; a leftover one becomes "paused" (not "failed": nothing went
// wrong and the pending photos can be indexed later), which stops the poller.
static void mark_index_paused_unless_active() {
    std::lock_guard<std::mutex> lock(g_index_state_mutex);
    if (g_index_status != "processing" && g_index_status != "loading_models") {
        g_index_status = "paused";
    }
}

extern "C" int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg, const void *pApi);

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_generateEmbeddingsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath);

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_generateEmbeddingsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {
    return Java_expo_modules_vantaengine_VantaEngineModule_generateEmbeddingsNative(env, nullptr, dbPath);
}

/**
 * generateEmbeddingsNative
 * Main background worker function that iterates over unindexed images in the database.
 * For each image, it extracts CLIP and Face embeddings using ONNX, stores them in SQLite,
 * and clusters identical faces. It also runs a final merge pass when indexing finishes.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_generateEmbeddingsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    std::unique_lock<std::mutex> job_lock(g_job_mutex, std::try_to_lock);
    if (!job_lock.owns_lock()) {
        LOGE("Indexing not started: another indexing run or a face re-scan is in progress");
        mark_index_paused_unless_active();
        return JNI_FALSE;
    }

    // An exception leaving a JNI function terminates the process, so nothing
    // may escape this block.
    sqlite3* db = nullptr;
    try {
        // Replaces the terminal status left by the previous run before any work,
        // so the JS poller never mistakes it for the outcome of this run.
        set_index_status("loading_models");
        // Cleared here rather than after model loading, so a pause requested while
        // the models load is honoured instead of being overwritten.
        g_pause_embedding.store(false);

        // Every failure return below reports "failed" so the JS poller can stop.
        if (dbPath == nullptr) {
            set_index_status("failed");
            return JNI_FALSE;
        }

        const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
        if (db_path_cstr == nullptr) {
            set_index_status("failed");
            return JNI_FALSE;
        }
        std::string db_path(db_path_cstr);
        env->ReleaseStringUTFChars(dbPath, db_path_cstr);

        // Ensure sqlite-vec is registered before creating/opening the DB
        sqlite3_auto_extension((void (*)())sqlite3_vec_init);

        db = initialize_database(db_path);
        if (!db) {
            LOGE("Failed to open DB for indexing");
            set_index_status("failed");
            return JNI_FALSE;
        }

        if (!init_clip_db(db)) {
            LOGE("Failed to init vector db");
            set_index_status("failed");
            sqlite3_close(db);
            return JNI_FALSE;
        }

        {
            std::lock_guard<std::mutex> lock(g_session_mutex);

            if (!g_clip_session) {
                g_clip_session = new CLIP_session();
            }

            if (!g_clip_session->is_loaded()) {
                if (!g_clip_session->load()) {
                    LOGE("Failed to load CLIP model");
                    set_index_status("failed");
                    sqlite3_close(db);
                    return JNI_FALSE;
                }
            }

            if (!g_face_session) {
                g_face_session = new Face_embedding();
            }
            if (!g_face_session->is_loaded()) {
                if (!g_face_session->load()) {
                    LOGE("Failed to load Face model");
                    set_index_status("failed");
                    sqlite3_close(db);
                    return JNI_FALSE;
                }
            }
        }

        LOGI("Starting embedding generation...");
        int total_to_process = get_picture_count(db_path);
        int unindexed_count = get_unindexed_picture_count(db_path);

        g_index_total_count.store(total_to_process);
        g_index_processed_count.store(total_to_process - unindexed_count);
        set_index_status("processing");

        // Pause is only checked here, between batches, so it never interrupts
        // a photo's transaction.
        while (!g_pause_embedding.load()) {
            std::vector<file_meta> images = get_unindexed_images(db_path, 10);
            if (images.empty()) break;

            // Photos that are neither indexed nor marked failed stay pending
            // and come back in the next batch; a batch where that happens to
            // every photo would repeat forever.
            bool progressed = false;

            for (const auto& img_meta : images) {
                std::string path = img_meta.content_uri;
                set_current_processing_file(path);

                // One transaction per photo: the CLIP embedding, the 'indexed'
                // status, the faces, their clusters and the graph edges are
                // committed together, so a photo is never left 'indexed' with
                // half of its face data (it is only selected while 'pending').
                bool in_transaction = false;
                bool indexed = false;
                try {
                    // All models (CLIP, face detection and embedding) run before
                    // the write lock is taken, so the transaction below only
                    // writes; this also gives other writers a turn between two photos.
                    // Decoded once for both pipelines; neither modifies it (each works on
                    // resized copies). An unreadable photo is empty here, and get_embedding
                    // throws on it, so the photo is marked failed below.
                    cv::Mat decoded = cv::imread(path);
                    CLIP_instance clip_img(decoded);
                    std::vector<float> embedding = g_clip_session->get_embedding(clip_img);
                    std::vector<ComputedFace> faces;
                    bool faces_read = compute_faces(decoded, path, img_meta.id, *g_face_session, faces);

                    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                        // Nothing was written; the photo stays pending and is retried.
                        LOGE("Failed to begin transaction for %s: %s", path.c_str(), sqlite3_errmsg(db));
                        continue;
                    }
                    in_transaction = true;

                    bool ok = save_clip_embedding(db, img_meta.id, embedding);
                    if (!ok) {
                        LOGE("Failed to save embedding for %s", path.c_str());
                    } else if (faces_read) {
                        store_faces(db, img_meta.id, faces);
                        // Fails the photo: unassigned faces could later only join existing
                        // people (recluster_pending_faces never creates one).
                        ok = cluster_faces_for_file(db, img_meta.id);
                        if (!ok) {
                            LOGE("Failed to cluster faces for %s", path.c_str());
                        } else if (!update_graph_for_file(db, img_meta.id)) {
                            // Does not fail the photo: the edges are derived data
                            // and the end-of-run rebuild_graph restores them.
                            LOGE("Failed to update graph for %s", path.c_str());
                        }
                    } else {
                        // Unreadable for the face models: stored nothing, the photo is indexed without faces.
                        LOGE("Failed to run face pipeline for %s", path.c_str());
                    }

                    if (ok) {
                        indexed = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
                        if (!indexed) LOGE("Failed to commit %s: %s", path.c_str(), sqlite3_errmsg(db));
                    }
                } catch (const std::exception& e) {
                    LOGE("Error processing image %s: %s", path.c_str(), e.what());
                } catch (...) {
                    LOGE("Unknown error processing image %s", path.c_str());
                }

                if (indexed) {
                    g_index_processed_count++;
                    g_photos_since_passes++;
                    progressed = true;
                    continue;
                }

                // The rollback comes first so that the 'failed' mark is a
                // statement of its own and is not undone with the photo's rows.
                // The mark bumps retry_count; insert_files sets the photo back to
                // pending on the next scans until it has failed 3 times.
                if (in_transaction) sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                if (update_file_status(db, img_meta.id, "failed")) progressed = true;
            }

            if (!progressed) {
                LOGE("No photo of the batch could be indexed or marked failed; stopping the run");
                set_index_status("failed");
                sqlite3_close(db);
                return JNI_FALSE;
            }
        }

        if (g_pause_embedding.load()) {
            // The end-of-run passes are skipped: a pause should return quickly,
            // every committed photo is complete and its graph edges are already
            // in place, and the passes run when the resumed run finishes.
            set_index_status("paused");
            LOGI("Embedding generation paused by user.");
        } else if (g_photos_since_passes == 0) {
            // Nothing was indexed since the passes last ran, so recluster and
            // rebuild would find nothing new; they cost O(pending faces x
            // people) on every run. A merge allowed only by a rename made
            // since then waits for the next run that indexes a photo.
            LOGI("No new photos since the last end-of-run passes; skipping them.");
            set_index_status("finished");
        } else {
            // No photo transaction is open here; the merge opens its own.
            LOGI("Finished generating embeddings for all images. Running second pass reclustering...");
            recluster_pending_faces(db);
            LOGI("Second pass reclustering finished. Running entity merge pass...");
            merge_similar_entities(db, g_owner_entity_id.load());
            LOGI("Entity merge pass finished. Rebuilding graph...");
            // Reclustering and merging re-assign faces in bulk, which the
            // per-photo graph updates never saw.
            if (!rebuild_graph(db)) {
                LOGE("Graph rebuild failed; the graph is incomplete until the next run finishes");
            } else {
                g_photos_since_passes = 0;
            }
            // Only now: "finished" is terminal for the poller, and the job
            // lock is held until the passes are done.
            set_index_status("finished");
        }

        sqlite3_close(db);
        return JNI_TRUE;
    } catch (const std::exception& e) {
        LOGE("Indexing aborted by an exception: %s", e.what());
    } catch (...) {
        LOGE("Indexing aborted by an unknown exception");
    }

    // Closing the connection rolls back a transaction that was left open.
    if (db) sqlite3_close(db);
    set_index_status("failed");
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_pauseEmbeddingsNative(
    JNIEnv* env,
    jobject /* this */) {
    g_pause_embedding.store(true);
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getIndexProgressNative(
    JNIEnv* env,
    jobject /* this */) {

    // Build a safe JSON object for the current indexing progress. Strings are
    // escaped because currentFile may contain arbitrary file paths.
    std::string status, current_file;
    get_index_state(status, current_file);

    std::string json = "{" +
        json_number_field("processed", g_index_processed_count.load()) + "," +
        json_number_field("total", g_index_total_count.load()) + "," +
        json_string_field("status", status) + "," +
        json_string_field("currentFile", current_file) +
        "}";

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getDatabaseStatsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    if (dbPath == nullptr) return env->NewStringUTF("{}");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    std::string json_result = get_database_stats_json(db_path);

    return env->NewStringUTF(json_result.c_str());
}

/**
 * searchImagesNative
 * Executes a semantic search query against the SQLite vector database.
 * Loads the CLIP tokenizer and NER models if necessary, converts the query to an embedding,
 * and returns the top matches as a JSON array string to the React Native layer.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_searchImagesNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jstring queryStr) {

    if (dbPath == nullptr || queryStr == nullptr) return env->NewStringUTF("[]");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    const char* query_cstr = env->GetStringUTFChars(queryStr, nullptr);
    std::string query(query_cstr);
    env->ReleaseStringUTFChars(queryStr, query_cstr);

    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (!g_clip_session) {
            g_clip_session = new CLIP_session();
        }

        if (!g_clip_session->is_loaded()) {
            LOGI("Loading CLIP model for search...");
            if (!g_clip_session->load()) {
                LOGE("Failed to load CLIP model for search");
                return env->NewStringUTF("[]");
            }
            LOGI("CLIP model loaded successfully for search.");
        }

        if (!g_tokenizer) {
            g_tokenizer = new CLIPTokenizer();
        }

        // Loaded under g_session_mutex: concurrent cold-start searches must
        // not both mutate the tokenizer's maps.
        if (!g_tokenizer->is_loaded()) {
            LOGI("Loading CLIP tokenizer...");
            const VantaConfig& cfg = VantaConfig::instance();
            std::string vocab_path = cfg.model_path("vocab.json");
            std::string merges_path = cfg.model_path("merges.txt");
            if (!g_tokenizer->load(vocab_path, merges_path)) {
                LOGE("Failed to load CLIP Tokenizer");
                return env->NewStringUTF("[]");
            }
            LOGI("CLIP tokenizer loaded successfully.");
        }
    }

    // Lazy-init NER model (mutex-protected, same pattern as CLIP)
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (!g_ner_model) {
            g_ner_model = new vanta::ner::NERModel();
        }

        if (!g_ner_model->is_loaded()) {
            const VantaConfig& cfg = VantaConfig::instance();
            std::string ner_model_path = cfg.model_path("ner_model.onnx");
            std::string ner_vocab_path = cfg.model_path("ner_vocab.txt");
            std::string ner_label_path = cfg.model_path("label_map.json");
            if (!g_ner_model->load(ner_model_path, ner_vocab_path, ner_label_path)) {
                LOGE("Failed to load NER model (non-fatal, search will skip NER)");
                // Non-fatal: search falls back to global KNN
            }
        }
    }

    vanta::ner::NERModel* ner_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        if (g_ner_model && g_ner_model->is_loaded()) {
            ner_ptr = g_ner_model;
        }
    }

    std::vector<search_result> results = search_images(
        db_path, query, g_clip_session, g_tokenizer,
        ner_ptr, g_owner_entity_id.load());

    LOGI("Building JSON response for %zu results", results.size());

    // Build the JSON array using the safe helper so file paths and display
    // names containing quotes or backslashes do not corrupt the response.
    std::string json = "[";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (i > 0) json += ",";
        json += "{" +
            json_number_field("file_id", r.file_id) + "," +
            json_string_field("abs_path", r.abs_path) + "," +
            json_string_field("display_name", r.display_name) + "," +
            json_number_field("size_bytes", r.size_bytes) + "," +
            json_number_field("mtime_unix", r.mtime_unix) + "," +
            json_number_field("distance", r.distance) +
            "}";
    }
    json += "]";

    LOGI("Returning JSON (%zu chars) with %zu results", json.size(), results.size());

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getTopEntitiesNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    if (dbPath == nullptr) return env->NewStringUTF("[]");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getTopEntitiesNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("[]");
    }

    std::vector<EntityMeta> entities = get_top_entities(db, 50);

    sqlite3_close(db);

    std::string json = "[";
    for (size_t i = 0; i < entities.size(); ++i) {
        if (i > 0) json += ",";
        json += "{" +
            json_number_field("entity_id", entities[i].entity_id) + "," +
            json_string_field("display_name", entities[i].display_name) + "," +
            json_number_field("sample_count", entities[i].sample_count) + "," +
            json_number_field("confidence", entities[i].confidence) +
            "}";
    }
    json += "]";

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getBestFaceCropNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId) {

    if (dbPath == nullptr) return env->NewStringUTF("{}");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    sqlite3* db = nullptr;
    // Read-write: get_best_face_crop stores the path of a crop it makes on demand.
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getBestFaceCropNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("{}");
    }
    sqlite3_busy_timeout(db, 5000);

    FaceCrop crop = get_best_face_crop(db, static_cast<int64_t>(entityId));

    sqlite3_close(db);

    if (crop.file_id == -1) {
        return env->NewStringUTF("{}");
    }

    std::string json = "{" +
        json_number_field("file_id", crop.file_id) + "," +
        json_string_field("abs_path", crop.abs_path) + "," +
        json_number_field("bbox_x", crop.bbox_x) + "," +
        json_number_field("bbox_y", crop.bbox_y) + "," +
        json_number_field("bbox_w", crop.bbox_w) + "," +
        json_number_field("bbox_h", crop.bbox_h) + "," +
        json_string_field("aligned_crop_path", crop.aligned_crop_path) +
        "}";

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setEntityNameNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId,
    jstring name) {

    if (dbPath == nullptr || name == nullptr) return JNI_FALSE;

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    const char* name_cstr = env->GetStringUTFChars(name, nullptr);
    std::string entity_name(name_cstr);
    env->ReleaseStringUTFChars(name, name_cstr);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for setEntityNameNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return JNI_FALSE;
    }
    sqlite3_busy_timeout(db, 5000);

    bool success = set_entity_name(db, static_cast<int64_t>(entityId), entity_name);
    if (success) {
        rebuild_query_engine_dictionary(db_path);
    }

    sqlite3_close(db);

    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getEntityNeighborsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId) {

    if (dbPath == nullptr) return env->NewStringUTF("[]");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getEntityNeighborsNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("[]");
    }

    std::vector<NeighborResult> neighbors = get_neighbors(db, static_cast<int64_t>(entityId), 20);

    sqlite3_close(db);

    std::string json = "[";
    for (size_t i = 0; i < neighbors.size(); ++i) {
        if (i > 0) json += ",";
        json += "{" +
            json_number_field("neighbor_id", neighbors[i].neighbor_id) + "," +
            json_string_field("display_name", neighbors[i].display_name) + "," +
            json_number_field("co_occurrence_count", neighbors[i].co_occurrence_count) +
            "}";
    }
    json += "]";

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getEntityFilesNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId) {

    if (dbPath == nullptr) return env->NewStringUTF("[]");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getEntityFilesNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("[]");
    }

    std::vector<EntityFile> files = get_entity_files(db, static_cast<int64_t>(entityId), 100);

    sqlite3_close(db);

    std::string json = "[";
    for (size_t i = 0; i < files.size(); ++i) {
        if (i > 0) json += ",";
        json += "{" +
            json_number_field("file_id", files[i].file_id) + "," +
            json_string_field("abs_path", files[i].abs_path) +
            "}";
    }
    json += "]";

    return env->NewStringUTF(json.c_str());
}

// Named Entity Recognition (NER) bindings
// These JNI functions allow the React Native layer to update entity metadata (like name, relation, age)
// and query or set the "owner" entity for personalized searches.

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setEntityMetadataNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId,
    jstring name,
    jstring relation,
    jint age,
    jstring location) {

    if (dbPath == nullptr || name == nullptr) return JNI_FALSE;

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    const char* name_cstr = env->GetStringUTFChars(name, nullptr);
    std::string entity_name(name_cstr);
    env->ReleaseStringUTFChars(name, name_cstr);

    std::string entity_relation;
    if (relation != nullptr) {
        const char* rel_cstr = env->GetStringUTFChars(relation, nullptr);
        entity_relation = rel_cstr;
        env->ReleaseStringUTFChars(relation, rel_cstr);
    }

    std::string entity_location;
    if (location != nullptr) {
        const char* loc_cstr = env->GetStringUTFChars(location, nullptr);
        entity_location = loc_cstr;
        env->ReleaseStringUTFChars(location, loc_cstr);
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for setEntityMetadataNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return JNI_FALSE;
    }
    sqlite3_busy_timeout(db, 5000);

    bool success = set_entity_metadata(db, static_cast<int64_t>(entityId),
                                       entity_name, entity_relation,
                                       static_cast<int>(age), entity_location);
    if (success) {
        rebuild_query_engine_dictionary(db_path);
    }

    sqlite3_close(db);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getEntityMetadataNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,
    jlong entityId) {

    if (dbPath == nullptr) return env->NewStringUTF("{}");

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getEntityMetadataNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("{}");
    }

    EntityMetadata meta = get_entity_metadata(db, static_cast<int64_t>(entityId));

    sqlite3_close(db);

    std::string json = "{" +
        json_number_field("entity_id", meta.entity_id) + "," +
        json_string_field("display_name", meta.display_name) + "," +
        json_string_field("relation", meta.relation) + "," +
        json_number_field("age", meta.age) + "," +
        json_string_field("location", meta.location) + "," +
        json_number_field("sample_count", meta.sample_count) + "," +
        json_number_field("confidence", meta.confidence) +
        "}";

    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setOwnerEntityIdNative(
    JNIEnv* env,
    jobject /* this */,
    jlong entityId) {

    g_owner_entity_id.store(static_cast<int64_t>(entityId));
    LOGI("Owner entity ID set to: %ld", (long)entityId);
}

extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setSearchOptionsNative(
    JNIEnv* env,
    jobject /* this */,
    jboolean useGraph,
    jboolean useSpellCheck,
    jboolean useIntent) {
    
    set_query_options(useGraph, useSpellCheck, useIntent);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_resetFaceDataNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    if (dbPath == nullptr) return JNI_FALSE;

    std::unique_lock<std::mutex> job_lock(g_job_mutex, std::try_to_lock);
    if (!job_lock.owns_lock()) {
        LOGE("Face re-scan not started: indexing or another re-scan is in progress");
        return JNI_FALSE;
    }

    // An exception leaving a JNI function terminates the process, so nothing
    // may escape this block.
    sqlite3* db = nullptr;
    try {
        const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
        if (db_path_cstr == nullptr) return JNI_FALSE;
        std::string db_path(db_path_cstr);
        env->ReleaseStringUTFChars(dbPath, db_path_cstr);

        // Ensure sqlite-vec is registered
        sqlite3_auto_extension((void (*)())sqlite3_vec_init);

        db = initialize_database(db_path);
        if (!db) {
            LOGE("Failed to open DB for resetFaceData");
            return JNI_FALSE;
        }

        if (!init_face_schema(db)) {
            LOGE("Failed to init face schema for reset");
            sqlite3_close(db);
            return JNI_FALSE;
        }

        // Loaded before anything is deleted: without the model the faces
        // could not be rebuilt.
        {
            std::lock_guard<std::mutex> lock(g_session_mutex);
            if (!g_face_session) {
                g_face_session = new Face_embedding();
            }
            if (!g_face_session->is_loaded()) {
                if (!g_face_session->load()) {
                    LOGE("Failed to load Face model for re-indexing");
                    sqlite3_close(db);
                    return JNI_FALSE;
                }
            }
        }

        // Wipe all face data
        if (!reset_face_data(db)) {
            LOGE("Failed to reset face data");
            sqlite3_close(db);
            return JNI_FALSE;
        }

        // Every entity is gone, the owner ("Me") entity included.
        g_owner_entity_id.store(-1);

        // Delete crop files from disk
        std::string crops_dir = VantaConfig::instance().crops_dir();
        if (!crops_dir.empty()) {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(crops_dir, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code remove_ec;
                std::filesystem::remove_all(it->path(), remove_ec);
                if (remove_ec) LOGE("Failed to remove %s: %s", it->path().c_str(), remove_ec.message().c_str());
            }
            if (ec) {
                LOGE("Failed to list crop files in %s: %s", crops_dir.c_str(), ec.message().c_str());
            } else {
                LOGI("Cleared crop files from: %s", crops_dir.c_str());
            }
        }

        // Re-run face pipeline on all indexed picture files (skip CLIP)
        LOGI("Re-running face pipeline on all indexed images...");

        // Query all indexed picture files. They are read into memory first: a
        // cursor left open across the writes below pins this connection to an
        // old snapshot, and its writes then fail as soon as any other
        // connection commits.
        std::vector<std::pair<int64_t, std::string>> files;
        const char* sql = "SELECT id, abs_path FROM files WHERE filetype = 'picture' AND status = 'indexed'";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LOGE("Failed to query indexed files for face re-processing: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            return JNI_FALSE;
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (!path) continue;
            files.emplace_back(sqlite3_column_int64(stmt, 0), path);
        }
        sqlite3_finalize(stmt);

        int processed = 0;
        for (const auto& file : files) {
            int64_t file_id = file.first;
            const std::string& abs_path = file.second;

            // One transaction per file, so a file never keeps half of its
            // faces. The face models run first, without the write lock, so
            // the transaction only writes and takes the lock up front
            // (IMMEDIATE), the same as the indexing loop, so it waits on the
            // busy timeout at BEGIN instead of failing part-way through.
            bool in_transaction = false;
            bool done = false;
            try {
                std::vector<ComputedFace> faces;
                // Unreadable for the face models: nothing to store, the file
                // is left without faces.
                if (!compute_faces(abs_path, file_id, *g_face_session, faces)) continue;

                if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                    LOGE("Failed to begin transaction for %s: %s", abs_path.c_str(), sqlite3_errmsg(db));
                    continue;
                }
                in_transaction = true;

                store_faces(db, file_id, faces);
                bool ok = cluster_faces_for_file(db, file_id);
                if (!ok) LOGE("Failed to cluster faces for %s", abs_path.c_str());

                if (ok) {
                    done = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
                    if (!done) LOGE("Failed to commit %s: %s", abs_path.c_str(), sqlite3_errmsg(db));
                    if (done) processed++;
                }
            } catch (const std::exception& e) {
                LOGE("Error re-processing faces of %s: %s", abs_path.c_str(), e.what());
            } catch (...) {
                LOGE("Unknown error re-processing faces of %s", abs_path.c_str());
            }

            // A file that failed is left without faces and the re-scan goes on.
            if (in_transaction && !done) sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            if (processed % 100 == 0 && processed > 0) {
                LOGI("Face re-processing progress: %d files", processed);
            }
        }

        LOGI("Face re-processing complete: %d files processed. Running recluster...", processed);
        recluster_pending_faces(db);
        // Same end-of-run passes as an indexing run; the graph was emptied by
        // the reset and is built here from the new detections.
        merge_similar_entities(db, g_owner_entity_id.load());
        if (!rebuild_graph(db)) {
            LOGE("Graph rebuild failed after face re-scan");
        } else {
            // The passes cover every photo, including any a paused run left.
            g_photos_since_passes = 0;
        }
        LOGI("Face data reset and re-indexed successfully.");

        sqlite3_close(db);
        return JNI_TRUE;
    } catch (const std::exception& e) {
        LOGE("Face re-scan aborted by an exception: %s", e.what());
    } catch (...) {
        LOGE("Face re-scan aborted by an unknown exception");
    }

    // Closing the connection rolls back a transaction that was left open.
    if (db) sqlite3_close(db);
    return JNI_FALSE;
}

// ── Documents ──

static std::string jstring_to_std(JNIEnv* env, jstring s) {
    if (s == nullptr) return "";
    const char* c = env->GetStringUTFChars(s, nullptr);
    if (c == nullptr) return "";
    std::string out(c);
    env->ReleaseStringUTFChars(s, c);
    return out;
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_IndexingService_getPendingDocumentsNative(
    JNIEnv* env, jobject /* this */, jstring dbPath) {
    return env->NewStringUTF(get_pending_documents_json(jstring_to_std(env, dbPath)).c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_saveDocumentTextNative(
    JNIEnv* env, jobject /* this */, jstring dbPath, jlong fileId, jstring name, jstring body) {
    return save_document_text(jstring_to_std(env, dbPath), fileId,
                              jstring_to_std(env, name), jstring_to_std(env, body))
        ? JNI_TRUE : JNI_FALSE;
}

// Lets document indexing honour the same pause as the photo loop; the flag is
// cleared at the start of each generateEmbeddingsNative run.
extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_isPauseRequestedNative(
    JNIEnv* env, jobject /* this */) {
    return g_pause_embedding.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_searchDocumentsNative(
    JNIEnv* env, jobject /* this */, jstring dbPath, jstring query) {
    return env->NewStringUTF(
        search_documents_json(jstring_to_std(env, dbPath), jstring_to_std(env, query)).c_str());
}
