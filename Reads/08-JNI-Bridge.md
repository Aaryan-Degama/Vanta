# 08 — JNI Bridge

> How Kotlin (Android) and C++ (ML Engine) communicate through the Java Native Interface.

---

## Overview

Vanta uses **JNI (Java Native Interface)** to bridge the React Native / Kotlin layer with the C++ ML engine. The bridge is implemented in `vanta.cpp` and exposed through the `VantaEngineModule` Kotlin class.



---

## JNI Method Registration

JNI methods are registered via **dynamic registration** (not `javah` stubs). This is more robust and avoids name mangling issues.



```cpp
// vanta.cpp
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    // Cache frequently used class references
    // Register native methods
    return JNI_VERSION_1_6;
}
```

---

## Native Method Table



---

## Type Conversion

### Java/Kotlin → C++

| Java/Kotlin Type | JNI Type | C++ Type | Conversion |
|------------------|----------|----------|------------|
| `String` | `jstring` | `std::string` | `env->GetStringUTFChars()` |
| `long` | `jlong` | `int64_t` | Direct cast |
| `int` | `jint` | `int` | Direct cast |
| `float` | `jfloat` | `float` | Direct cast |
| `boolean` | `jboolean` | `bool` | Direct cast |
| `FileMeta[]` | `jobjectArray` | `std::vector<file_meta>` | Reflection per element |
| `HashMap<String, String>` | `jobject` | `std::unordered_map<std::string, std::string>` | Reflection per entry |
| `List<String>` | `jobject` | `std::vector<std::string>` | Reflection per element |

### C++ → Java/Kotlin

| C++ Type | JNI Type | Java/Kotlin Type | Conversion |
|----------|----------|------------------|------------|
| `std::string` | `jstring` | `String` | `env->NewStringUTF()` |
| `std::vector<search_result>` | `jstring` | `String` (JSON) | `nlohmann::json::dump()` |
| `std::vector<entity_result>` | `jstring` | `String` (JSON) | `nlohmann::json::dump()` |
| `std::vector<face_crop>` | `jstring` | `String` (JSON) | `nlohmann::json::dump()` |
| `int` | `jint` | `int` | Direct return |
| `bool` | `jboolean` | `boolean` | Direct return |

---

## FileMeta Array Conversion (Detailed)



**C++ struct:**
```cpp
struct file_meta {
    int64_t media_id;
    std::string content_uri;
    std::string abs_path;
    std::string display_name;
    std::string filetype;
    std::string mime_type;
    int64_t size_bytes;
    int64_t mtime_unix;
    int64_t last_indexed_at;
    int width_px;
    int height_px;
    int64_t duration_ms;
    std::string status;
    int retry_count;
    int face_count;
};
```

---

## JSON Response Format

All complex return types are serialized as JSON strings:

### Search Results
```json
[
  {
    "file_id": 101,
    "abs_path": "/storage/emulated/0/DCIM/IMG_001.jpg",
    "display_name": "IMG_001.jpg",
    "size_bytes": 2048000,
    "mtime_unix": 1704067200,
    "distance": 0.1234
  }
]
```

### People List
```json
[
  {
    "entity_id": 1,
    "display_name": "Aaryan",
    "relation": "",
    "sample_count": 42,
    "best_face_crop": {
      "file_id": 101,
      "abs_path": "/storage/.../IMG_001.jpg",
      "bbox_x": 120,
      "bbox_y": 80,
      "bbox_w": 200,
      "bbox_h": 200
    }
  }
]
```

### Entity Detail
```json
{
  "entity_id": 1,
  "display_name": "Aaryan",
  "relation": "",
  "sample_count": 42,
  "best_face_crop": { ... },
  "photos": [
    { "file_id": 101, "abs_path": "...", "distance": 0.0 },
    { "file_id": 205, "abs_path": "...", "distance": 0.15 }
  ],
  "co_occurring_entities": [
    { "entity_id": 2, "display_name": "Mom", "count": 15 },
    { "entity_id": 3, "display_name": "Dad", "count": 8 }
  ]
}
```

---

## Thread Safety



**Rules:**
1. `g_session_mutex` is acquired only for **model load/unload** operations
2. **Inference is read-only** and does not need the mutex
3. Multiple inference calls can run concurrently (ONNX Runtime is thread-safe)
4. SQLite handles its own concurrency via WAL mode

---

## Error Handling

| Scenario | JNI Behavior | JavaScript Result |
|----------|-------------|-------------------|
| Model not loaded | Return empty JSON `[]` | Empty results grid |
| DB not found | Log error, return empty | Empty results |
| Invalid query | Return empty JSON `[]` | Empty results |
| JNI exception | `env->ExceptionClear()` + return error | Promise rejected |
| Out of memory | `std::bad_alloc` caught | Return empty + log |
| SQLite busy | Retry with backoff | Slight delay |

---

## Memory Management



**Critical JNI rules:**
- `GetStringUTFChars` → must `ReleaseStringUTFChars`
- `GetObjectArrayElement` → local ref, auto-freed but watch 512 limit
- `FindClass` → returns local ref; cache as global ref if needed long-term
- `NewStringUTF` → Java owns the string; C++ can discard after return

---

## Lifecycle



---

## Adding a New JNI Method

To add a new native method:

1. **Declare in Kotlin** (`VantaEngineModule.kt`):
```kotlin
external fun myNewMethodNative(dbPath: String, param: String): String
```

2. **Implement in C++** (`vanta.cpp`):
```cpp
extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_myNewMethodNative(
    JNIEnv* env,
    jobject thiz,
    jstring j_db_path,
    jstring j_param
) {
    const char* db_path = env->GetStringUTFChars(j_db_path, nullptr);
    const char* param = env->GetStringUTFChars(j_param, nullptr);
    
    // ... business logic ...
    
    env->ReleaseStringUTFChars(j_db_path, db_path);
    env->ReleaseStringUTFChars(j_param, param);
    
    return env->NewStringUTF(result_json.c_str());
}
```

3. **Register in JNI_OnLoad** (if using dynamic registration):
```cpp
static JNINativeMethod methods[] = {
    {"myNewMethodNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", 
     (void*)Java_expo_modules_vantaengine_VantaEngineModule_myNewMethodNative},
    // ... other methods
};
```

4. **Expose to TypeScript** (`src/index.ts`):
```typescript
myNewMethod(dbPath: string, param: string): Promise<string> {
    return VantaEngineModule.myNewMethodNative(dbPath, param);
}
```
