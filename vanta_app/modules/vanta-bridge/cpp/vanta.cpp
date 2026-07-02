#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
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
static int64_t g_owner_entity_id = -1;

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
static std::string g_current_processing_file = "";
static std::string g_index_status = "idle";
static std::atomic<bool> g_pause_embedding(false);

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

extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_generateEmbeddingsNative(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath) {

    if (dbPath == nullptr) return JNI_FALSE;

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    // Ensure sqlite-vec is registered before creating/opening the DB
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);

    sqlite3* db = initialize_database(db_path);
    if (!db) {
        LOGE("Failed to open DB for indexing");
        return JNI_FALSE;
    }

    if (!init_clip_db(db)) {
        LOGE("Failed to init vector db");
        sqlite3_close(db);
        return JNI_FALSE;
    }

    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (!g_clip_session) {
            g_clip_session = new CLIP_session();
        }

        g_index_status = "loading_models";
        if (!g_clip_session->is_loaded()) {
            if (!g_clip_session->load()) {
                LOGE("Failed to load CLIP model");
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
    g_index_status = "processing";
    g_pause_embedding.store(false);

    while (!g_pause_embedding.load()) {
        std::vector<file_meta> images = get_unindexed_images(db_path, 10);
        if (images.empty()) break;

        for (const auto& img_meta : images) {
            std::string path = img_meta.content_uri; 
            g_current_processing_file = path;
            try {
                CLIP_instance clip_img(path);
                std::vector<float> embedding = g_clip_session->get_embedding(clip_img);
                
                if (save_clip_embedding(db, img_meta.id, embedding)) {
                    // Also run face pipeline
                    if (run_face_pipeline(db, path, img_meta.id, *g_face_session)) {
                        cluster_faces_for_file(db, img_meta.id);
                        update_graph_for_file(db, img_meta.id);
                    } else {
                        LOGE("Failed to run face pipeline for %s", path.c_str());
                    }

                    g_index_processed_count++;
                } else {
                    LOGE("Failed to save embedding for %s", path.c_str());
                    update_file_status(db, img_meta.id, "failed");
                }
            } catch (const std::exception& e) {
                LOGE("Error processing image %s: %s", path.c_str(), e.what());
                update_file_status(db, img_meta.id, "failed");
            }
        }
    }

    if (g_pause_embedding.load()) {
        g_index_status = "paused";
        LOGI("Embedding generation paused by user.");
    } else {
        g_index_status = "finished";
        LOGI("Finished generating embeddings for all images. Running second pass reclustering...");
        recluster_pending_faces(db);
        LOGI("Second pass reclustering finished. Running entity merge pass...");
        merge_similar_entities(db);
        LOGI("Entity merge pass finished.");
    }

    sqlite3_close(db);
    return JNI_TRUE;
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
    std::string json = "{" +
        json_number_field("processed", g_index_processed_count.load()) + "," +
        json_number_field("total", g_index_total_count.load()) + "," +
        json_string_field("status", g_index_status) + "," +
        json_string_field("currentFile", g_current_processing_file) +
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
    }

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
        ner_ptr, g_owner_entity_id);

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
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOGE("Failed to open DB for getBestFaceCropNative: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return env->NewStringUTF("{}");
    }

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

// ── NER pipeline: Entity metadata + Owner entity JNI ──

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

    g_owner_entity_id = static_cast<int64_t>(entityId);
    LOGI("Owner entity ID set to: %ld", (long)g_owner_entity_id);
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

    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    // Ensure sqlite-vec is registered
    sqlite3_auto_extension((void (*)())sqlite3_vec_init);

    sqlite3* db = initialize_database(db_path);
    if (!db) {
        LOGE("Failed to open DB for resetFaceData");
        return JNI_FALSE;
    }

    if (!init_face_schema(db)) {
        LOGE("Failed to init face schema for reset");
        sqlite3_close(db);
        return JNI_FALSE;
    }

    // Wipe all face data
    if (!reset_face_data(db)) {
        LOGE("Failed to reset face data");
        sqlite3_close(db);
        return JNI_FALSE;
    }

    // Delete crop files from disk
    std::string crops_dir = VantaConfig::instance().crops_dir();
    if (!crops_dir.empty()) {
        std::string cmd = "rm -rf " + crops_dir + "/*";
        system(cmd.c_str());
        LOGI("Cleared crop files from: %s", crops_dir.c_str());
    }

    // Re-run face pipeline on all indexed picture files (skip CLIP)
    LOGI("Re-running face pipeline on all indexed images...");

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

    // Query all indexed picture files
    const char* sql = "SELECT id, abs_path FROM files WHERE filetype = 'picture' AND status = 'indexed'";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE("Failed to query indexed files for face re-processing: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return JNI_FALSE;
    }

    int processed = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t file_id = sqlite3_column_int64(stmt, 0);
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!path) continue;

        std::string abs_path(path);
        if (run_face_pipeline(db, abs_path, file_id, *g_face_session)) {
            cluster_faces_for_file(db, file_id);
            processed++;
        }

        if (processed % 100 == 0 && processed > 0) {
            LOGI("Face re-processing progress: %d files", processed);
        }
    }
    sqlite3_finalize(stmt);

    LOGI("Face re-processing complete: %d files processed. Running recluster...", processed);
    recluster_pending_faces(db);
    LOGI("Face data reset and re-indexed successfully.");

    sqlite3_close(db);
    return JNI_TRUE;
}