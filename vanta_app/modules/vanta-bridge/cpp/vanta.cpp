#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include "DBoperations.hpp"
#include "clip_db.hpp"
#include "Query/Query_processing/query_engine.hpp"
#include "Preprocessing/CLIP/CLIP_model.hpp"
#include <atomic>
#include "Preprocessing/CLIP/CLIP_tokenizer.hpp"

static CLIPTokenizer* g_tokenizer = nullptr;


#define LOG_TAG "VantaEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
    std::vector<file_meta> files = get_files(db_path, 100);

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
        LOGI("Finished generating embeddings for all images.");
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
    
    std::string current_file = g_current_processing_file;
    std::string current_status = g_index_status;
    std::string json = "{\"processed\": " + std::to_string(g_index_processed_count.load()) + 
                       ", \"total\": " + std::to_string(g_index_total_count.load()) + 
                       ", \"status\": \"" + current_status + "\"" +
                       ", \"currentFile\": \"" + current_file + "\"}";
                       
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

    LOGI("searchImagesNative called: query='%s' db='%s'", query.c_str(), db_path.c_str());

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
    
    if (!g_tokenizer->is_loaded()) {
        LOGI("Loading CLIP tokenizer...");
        std::string vocab_path = "/data/user/0/com.aaryan_ka.VantaApp/files/VantaModels/vocab.json";
        std::string merges_path = "/data/user/0/com.aaryan_ka.VantaApp/files/VantaModels/merges.txt";
        if (!g_tokenizer->load(vocab_path, merges_path)) {
            LOGE("Failed to load CLIP Tokenizer");
            return env->NewStringUTF("[]");
        }
        LOGI("CLIP tokenizer loaded successfully.");
    }

    std::vector<int64_t> tokens = g_tokenizer->encode(query);
    LOGI("Tokenized query into %zu tokens. First few: [%lld, %lld, %lld ...]",
         tokens.size(),
         tokens.size() > 0 ? (long long)tokens[0] : -1LL,
         tokens.size() > 1 ? (long long)tokens[1] : -1LL,
         tokens.size() > 2 ? (long long)tokens[2] : -1LL);

    std::vector<search_result> results = search_images(db_path, tokens, g_clip_session);

    LOGI("Building JSON response for %zu results", results.size());

    std::string json = "[";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (i > 0) json += ",";
        json += "{";
        json += "\"file_id\":" + std::to_string(r.file_id) + ",";
        
        // Escape quotes and backslashes in strings just in case
        std::string safe_path = r.abs_path;
        std::string safe_name = r.display_name;
        
        json += "\"abs_path\":\"" + safe_path + "\",";
        json += "\"display_name\":\"" + safe_name + "\",";
        json += "\"size_bytes\":" + std::to_string(r.size_bytes) + ",";
        json += "\"mtime_unix\":" + std::to_string(r.mtime_unix) + ",";
        json += "\"distance\":" + std::to_string(r.distance);
        json += "}";
    }
    json += "]";

    LOGI("Returning JSON (%zu chars) with %zu results", json.size(), results.size());

    return env->NewStringUTF(json.c_str());
}