#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include "DB/DBoperations.hpp"

#define LOG_TAG "VantaEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

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

    jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
    if (fileMetaClass == nullptr) return JNI_FALSE;

    jfieldID contentUriField = env->GetFieldID(fileMetaClass, "contentUri", "Ljava/lang/String;");
    jfieldID fileDtypeField = env->GetFieldID(fileMetaClass, "fileDtype", "Lexpo/modules/vantaengine/FileType;");
    jfieldID mimeTypeField = env->GetFieldID(fileMetaClass, "mimeType", "Ljava/lang/String;");
    jfieldID sizeBytesField = env->GetFieldID(fileMetaClass, "sizeBytes", "J");
    jfieldID mtimeUnixField = env->GetFieldID(fileMetaClass, "mtimeUnix", "J");
    jfieldID lastIndexedAtField = env->GetFieldID(fileMetaClass, "lastIndexedAt", "J");
    jfieldID widthPxField = env->GetFieldID(fileMetaClass, "widthPx", "I");
    jfieldID heightPxField = env->GetFieldID(fileMetaClass, "heightPx", "I");
    jfieldID durationMsField = env->GetFieldID(fileMetaClass, "durationMs", "Ljava/lang/Long;");
    jfieldID statusField = env->GetFieldID(fileMetaClass, "status", "Ljava/lang/String;");
    jfieldID retryCountField = env->GetFieldID(fileMetaClass, "retryCount", "I");

    jclass fileTypeClass = env->FindClass("expo/modules/vantaengine/FileType");
    jmethodID nameMethod = env->GetMethodID(fileTypeClass, "name", "()Ljava/lang/String;");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID longValueMethod = env->GetMethodID(longClass, "longValue", "()J");

    for (jsize i = 0; i < count; ++i) {
        jobject fileObj = env->GetObjectArrayElement(files, i);
        if (fileObj == nullptr) continue;

        file_meta meta;

        jstring jContentUri = (jstring)env->GetObjectField(fileObj, contentUriField);
        if (jContentUri != nullptr) {
            const char* contentUriCstr = env->GetStringUTFChars(jContentUri, nullptr);
            meta.content_uri = contentUriCstr;
            env->ReleaseStringUTFChars(jContentUri, contentUriCstr);
            env->DeleteLocalRef(jContentUri);
        }

        jobject jFileType = env->GetObjectField(fileObj, fileDtypeField);
        if (jFileType != nullptr) {
            jstring jFileTypeName = (jstring)env->CallObjectMethod(jFileType, nameMethod);
            const char* fileTypeNameCstr = env->GetStringUTFChars(jFileTypeName, nullptr);
            meta.file_type = fileTypeNameCstr;
            env->ReleaseStringUTFChars(jFileTypeName, fileTypeNameCstr);
            env->DeleteLocalRef(jFileTypeName);
            env->DeleteLocalRef(jFileType);
        }

        jstring jMimeType = (jstring)env->GetObjectField(fileObj, mimeTypeField);
        if (jMimeType != nullptr) {
            const char* mimeTypeCstr = env->GetStringUTFChars(jMimeType, nullptr);
            meta.mime_type = mimeTypeCstr;
            env->ReleaseStringUTFChars(jMimeType, mimeTypeCstr);
            env->DeleteLocalRef(jMimeType);
        }

        meta.size_bytes = env->GetLongField(fileObj, sizeBytesField);
        meta.mtime_unix = env->GetLongField(fileObj, mtimeUnixField);
        meta.last_indexed_at = env->GetLongField(fileObj, lastIndexedAtField);
        meta.width_px = env->GetIntField(fileObj, widthPxField);
        meta.height_px = env->GetIntField(fileObj, heightPxField);

        jobject jDurationMs = env->GetObjectField(fileObj, durationMsField);
        if (jDurationMs != nullptr) {
            meta.duration_ms = env->CallLongMethod(jDurationMs, longValueMethod);
            env->DeleteLocalRef(jDurationMs);
        } else {
            meta.duration_ms = 0;
        }

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