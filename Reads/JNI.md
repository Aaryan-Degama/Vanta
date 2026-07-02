# JNI (Java Native Interface) — Complete Guide for Android

> **Target Audience:** Android developers writing C/C++ bridges for performance-critical code, ML inference, or legacy library integration.  
> **Based on:** Real-world production code from the *VantaEngine* project (vision/face-search engine).

---

## Table of Contents
1. [What is JNI?](#1-what-is-jni)
2. [Why Use JNI on Android?](#2-why-use-jni-on-android)
3. [The JNI Bridge — How Java Talks to C++](#3-the-jni-bridge--how-java-talks-to-c)
4. [Naming Convention & Function Signature](#4-naming-convention--function-signature)
5. [The `JNIEnv*` Pointer — Your Swiss Army Knife](#5-the-jnienv-pointer--your-swiss-army-knife)
6. [Java ↔ C++ Type Mapping](#6-java--c-type-mapping)
7. [Method Signatures (The Cryptic Strings)](#7-method-signatures-the-cryptic-strings)
8. [Working with Strings](#8-working-with-strings)
9. [Working with Objects & Fields](#9-working-with-objects--fields)
10. [Working with Arrays](#10-working-with-arrays)
11. [Local vs Global References](#11-local-vs-global-references)
12. [Threading & The `g_session_mutex` Pattern](#12-threading--the-gsessionmutex-pattern)
13. [The Full Lifecycle: How It Runs on a Phone](#13-the-full-lifecycle-how-it-runs-on-a-phone)
14. [Android-Specifics: Logging & `extern "C"`](#14-android-specifics-logging--extern-c)
15. [Deep Dive: Two Real Functions from VantaEngine](#15-deep-dive-two-real-functions-from-vantaengine)
16. [Common Pitfalls & Best Practices](#16-common-pitfalls--best-practices)
17. [Quick Reference Cheat Sheet](#17-quick-reference-cheat-sheet)

---

## 1. What is JNI?

**JNI** = **Java Native Interface**. It is the official bridge that lets Java/Kotlin code call functions written in **C or C++** (and vice versa).

Think of it as a translator:
- **Java side:** `VantaEngineModule.searchImages("db.sqlite", "cat")`
- **JNI side:** `Java_expo_modules_vantaengine_VantaEngineModule_searchImagesNative(...)`
- **C++ side:** Runs CLIP inference, queries SQLite-vec, returns JSON

Without JNI, Java cannot directly execute C++ code. JNI handles:
- Memory layout differences
- Garbage collection (Java) vs manual memory (C++)
- Type conversions
- Exception propagation

---

## 2. Why Use JNI on Android?

| Reason | Example from VantaEngine |
|--------|--------------------------|
| **Performance** | CLIP image embedding (heavy matrix ops) |
| **Existing C++ libraries** | SQLite, OpenCV, ONNX Runtime, sqlite-vec |
| **Hardware access** | Direct GPU/NPU inference via C++ |
| **Cross-platform** | Same C++ core runs on iOS (via Objective-C++) |
| **Memory control** | Large model weights; avoid Java heap limits |

---

## 3. The JNI Bridge — How Java Talks to C++

### 3.1 The Three Layers

```
┌─────────────────────────────────────────┐
│  Layer 1: Java / Kotlin (Android App)   │
│  VantaEngineModule.java                 │
│  public native String searchImages(...) │
└─────────────────────────────────────────┘
                    │
                    ▼ (JNI Bridge)
┌─────────────────────────────────────────┐
│  Layer 2: C++ JNI Glue Code             │
│  vanta.cpp                              │
│  Java_expo_..._searchImagesNative(...)  │
│  • Converts Java types to C++           │
│  • Calls business logic                 │
│  • Converts C++ results back to Java    │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  Layer 3: Pure C++ Business Logic       │
│  CLIP_model.cpp, query_engine.cpp, etc.│
│  • No JNI knowledge needed here         │
│  • Can be unit-tested independently     │
└─────────────────────────────────────────┘
```

### 3.2 How the JVM Finds the Native Function

When Java calls `System.loadLibrary("vantaengine")`, the JVM:
1. Loads `libvantaengine.so` from the APK's `lib/<abi>/` folder
2. Scans exported symbols for names matching `Java_<package>_<class>_<method>`
3. Links the Java `native` method to the C function address

**Critical:** The C++ function name must match exactly, or you get `UnsatisfiedLinkError`.

---

## 4. Naming Convention & Function Signature

Every JNI function follows this exact naming rule:

```cpp
JNIEXPORT <return_type> JNICALL
Java_<package>_<class>_<method>(
    JNIEnv* env,       // ← JNI environment (thread-local)
    jobject thiz,       // ← 'this' reference (null for static)
    ...                 // ← Java arguments mapped to C++ types
);
```

### Example from VantaEngine

**Java side:**
```java
package expo.modules.vantaengine;
public class VantaEngineModule {
    public native void setModelsDirNative(String modelsDir);
}
```

**C++ side:**
```cpp
extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative(
    JNIEnv* env,
    jobject /* this */,   // Instance method, so 'this' is passed
    jstring modelsDir     // Java String → C++ jstring
);
```

**Name breakdown:**
```
Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative
│    └──────────────┬──────────────┘ └──────────┬──────────┘ └──────┬──────┘
│           package with underscores          class name      method name
│           (expo.modules.vantaengine)   (VantaEngineModule)  (setModelsDirNative)
```

> **Rule:** Replace `.` with `_` in package names. If the class name contains `_`, it becomes `_1` (escape sequence).

---

## 5. The `JNIEnv*` Pointer — Your Swiss Army Knife

`JNIEnv* env` is a **thread-local pointer** to a table of ~250 function pointers. It is the **only** way to interact with the JVM from C++.

**Key categories of `env->` operations:**

| Category | Examples | Purpose |
|----------|----------|---------|
| **Class ops** | `FindClass`, `GetMethodID`, `GetFieldID` | Reflect on Java classes |
| **Object ops** | `GetObjectField`, `SetObjectField` | Read/write object fields |
| **String ops** | `GetStringUTFChars`, `NewStringUTF` | Convert Java ↔ C strings |
| **Array ops** | `GetArrayLength`, `GetObjectArrayElement` | Handle Java arrays |
| **Primitive ops** | `GetIntField`, `GetLongField` | Read primitive fields |
| **Reference ops** | `NewGlobalRef`, `DeleteLocalRef` | Manage object lifetime |
| **Exception ops** | `ExceptionCheck`, `ExceptionClear` | Handle Java exceptions |

**Important:** `JNIEnv*` is **thread-local**. You cannot share it across threads. If you spawn a C++ thread, you must call `JavaVM->AttachCurrentThread()` to get a valid `JNIEnv*` for that thread.

---

## 6. Java ↔ C++ Type Mapping

| Java Type | JNI Type | C++ Equivalent |
|-----------|----------|----------------|
| `boolean` | `jboolean` | `unsigned char` (0/1) |
| `byte` | `jbyte` | `signed char` |
| `char` | `jchar` | `unsigned short` (UTF-16) |
| `short` | `jshort` | `short` |
| `int` | `jint` | `int` |
| `long` | `jlong` | `long long` (64-bit) |
| `float` | `jfloat` | `float` |
| `double` | `jdouble` | `double` |
| `Object` | `jobject` | opaque pointer |
| `String` | `jstring` | `jobject` subclass |
| `Class` | `jclass` | `jobject` subclass |
| `Object[]` | `jobjectArray` | `jobject` subclass |
| `int[]` | `jintArray` | primitive array |
| `void` | `void` | — |

---

## 7. Method Signatures (The Cryptic Strings)

JNI uses **internal descriptor strings** to identify methods and fields. These are required for `GetMethodID`, `GetFieldID`, etc.

### 7.1 Type Signatures

| Java | Signature |
|------|-----------|
| `boolean` | `Z` |
| `byte` | `B` |
| `char` | `C` |
| `short` | `S` |
| `int` | `I` |
| `long` | `J` |
| `float` | `F` |
| `double` | `D` |
| `void` | `V` |
| `String` | `Ljava/lang/String;` |
| `Object` | `Ljava/lang/Object;` |
| `int[]` | `[I` |
| `String[]` | `[Ljava/lang/String;` |
| `HashMap` | `Ljava/util/HashMap;` |
| `Long` (boxed) | `Ljava/lang/Long;` |

### 7.2 Method Signature Format

```
(<arg1><arg2>...)<return>
```

**Examples from VantaEngine:**

```cpp
// Java: HashMap() — no args, returns HashMap
"()Ljava/util/HashMap;"

// Java: HashMap.put(Object key, Object value) → Object
"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"

// Java: Long.longValue() → long
"()J"

// Java: String name() → String (for enum)
"()Ljava/lang/String;"

// Java: <init>(long) — Long constructor
"(J)V"
```

---

## 8. Working with Strings

Java `String` is **NOT** null-terminated UTF-8. It is a UTF-16 object. JNI provides conversion helpers.

### Pattern: Java String → C++ `std::string`

```cpp
extern "C" JNIEXPORT void JNICALL
Java_..._setModelsDirNative(JNIEnv* env, jobject, jstring modelsDir) {
    if (modelsDir == nullptr) return;

    // 1. Get a C-style UTF-8 pointer (temporary!)
    const char* models_dir_cstr = env->GetStringUTFChars(modelsDir, nullptr);

    // 2. Copy into a C++ string (safe, owned by C++)
    std::string cpp_str(models_dir_cstr);

    // 3. CRITICAL: Release the JNI string to prevent memory leak
    env->ReleaseStringUTFChars(modelsDir, models_dir_cstr);

    // Now use cpp_str safely...
}
```

### Pattern: C++ `std::string` → Java String

```cpp
std::string result = "hello";
return env->NewStringUTF(result.c_str());
```

> **⚠️ Memory Rule:** Every `GetStringUTFChars` **must** have a matching `ReleaseStringUTFChars`. The JVM pins the Java string in memory until released.

---

## 9. Working with Objects & Fields

### 9.1 Reading Fields from a Java Object

From VantaEngine's `startStoring`, we read fields from a `FileMeta` data class:

```cpp
// 1. Find the class
jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");

// 2. Get field IDs (do once, cache if possible)
jfieldID absPathField = env->GetFieldID(fileMetaClass, "absPath", "Ljava/lang/String;");
jfieldID sizeBytesField = env->GetFieldID(fileMetaClass, "sizeBytes", "J");

// 3. Read values from an instance
jstring jAbsPath = (jstring)env->GetObjectField(fileObj, absPathField);
jlong size = env->GetLongField(fileObj, sizeBytesField);
```

### 9.2 Creating Java Objects from C++

From VantaEngine's `getStoredFilesNative`:

```cpp
// 1. Find class and constructor
jclass hashMapClass = env->FindClass("java/util/HashMap");
jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");

// 2. Create instance
jobject map = env->NewObject(hashMapClass, hashMapInit);

// 3. Call method
jmethodID hashMapPut = env->GetMethodID(hashMapClass, "put",
    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
env->CallObjectMethod(map, hashMapPut, key, value);
```

> **Note:** `<init>` is the special name for constructors. The return type is always `V` (void).

### 9.3 Working with Java Enums

```cpp
// FileType enum in Java
// Java: fileDtype.name() returns String

jclass fileTypeClass = env->FindClass("expo/modules/vantaengine/FileType");
jmethodID nameMethod = env->GetMethodID(fileTypeClass, "name", "()Ljava/lang/String;");

jobject jFileType = env->GetObjectField(fileObj, fileDtypeField);
jstring jFileTypeName = (jstring)env->CallObjectMethod(jFileType, nameMethod);
```

---

## 10. Working with Arrays

### 10.1 Object Arrays (`jobjectArray`)

```cpp
// Java: FileMeta[] files
jobjectArray files = ...;
jsize count = env->GetArrayLength(files);

for (jsize i = 0; i < count; ++i) {
    jobject fileObj = env->GetObjectArrayElement(files, i);
    // ... process fileObj ...
    env->DeleteLocalRef(fileObj);  // Clean up!
}
```

### 10.2 Creating Object Arrays

```cpp
// Return HashMap[]
jclass hashMapClass = env->FindClass("java/util/HashMap");
jobjectArray resultArray = env->NewObjectArray(count, hashMapClass, nullptr);

for (jsize i = 0; i < count; ++i) {
    jobject map = env->NewObject(hashMapClass, initMethod);
    env->SetObjectArrayElement(resultArray, i, map);
    env->DeleteLocalRef(map);
}
```

---

## 11. Local vs Global References

This is the **#1 cause of JNI memory leaks**.

### 11.1 Local References

- Created by: `FindClass`, `NewObject`, `GetObjectField`, `NewStringUTF`, etc.
- **Scope:** Valid only in the current native method call
- **Auto-freed:** When the native method returns to Java
- **Limit:** 512 local references per frame (can overflow in loops!)

```cpp
// BAD: Loop creates 10,000 local refs without cleanup
for (int i = 0; i < 10000; i++) {
    jstring s = env->NewStringUTF("x");  // Local ref created
    // Missing: env->DeleteLocalRef(s);
}
// CRASH: Local reference table overflow
```

```cpp
// GOOD: Explicit cleanup
for (int i = 0; i < 10000; i++) {
    jstring s = env->NewStringUTF("x");
    // ... use s ...
    env->DeleteLocalRef(s);  // Free immediately
}
```

### 11.2 Global References

- Created by: `env->NewGlobalRef(obj)`
- **Scope:** Survives across multiple native calls
- **Must be freed:** `env->DeleteGlobalRef(g_ref)` when done
- **Use case:** Caching class objects, storing callbacks

In VantaEngine, global **raw pointers** are used (not JNI global refs) because the C++ objects (`CLIP_session`, `Face_embedding`) are pure C++ heap objects, not Java objects:

```cpp
static CLIP_session* g_clip_session = nullptr;  // C++ heap, not JNI ref
```

---

## 12. Threading & The `g_session_mutex` Pattern

### 12.1 The Problem

In VantaEngine, multiple Android threads can call JNI:
- **IndexingService** (background thread) calls `generateEmbeddingsNative`
- **UI thread** calls `searchImagesNative`
- Both need `g_clip_session`, but model loading is expensive and non-thread-safe

### 12.2 The Solution: Mutex + Lazy Initialization

```cpp
static std::mutex g_session_mutex;
static CLIP_session* g_clip_session = nullptr;

// Called from multiple threads
void someNativeFunction(JNIEnv* env, ...) {
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (!g_clip_session) {
            g_clip_session = new CLIP_session();  // Allocate
        }
        if (!g_clip_session->is_loaded()) {
            g_clip_session->load();  // Load model (heavy, one-time)
        }
    }  // Mutex released here

    // Now safe to use g_clip_session (read-only inference is thread-safe)
    g_clip_session->get_embedding(...);
}
```

### 12.3 JNIEnv & Threads

`JNIEnv*` is **thread-local**. If you spawn a C++ worker thread:

```cpp
// WRONG: Cannot share env across threads
std::thread([env]() {  // env is invalid in new thread!
    env->FindClass(...);  // CRASH
});

// CORRECT: Attach new thread to JVM
JavaVM* g_vm;  // Store this from JNI_OnLoad

std::thread([]() {
    JNIEnv* env;
    g_vm->AttachCurrentThread(&env, nullptr);
    // Now env is valid in this thread
    env->FindClass(...);
    g_vm->DetachCurrentThread();
});
```

---

## 13. The Full Lifecycle: How It Runs on a Phone

### 13.1 Build Time

```
vanta.cpp
    │
    ▼
Android NDK (Clang/LLVM)
    │
    ▼
libvantaengine.so  (ARM64, ARMv7, x86_64)
    │
    ▼
Packaged into APK → AAB → Play Store
```

### 13.2 Runtime

```
1. App launches
2. Java: System.loadLibrary("vantaengine")
3. JVM loads libvantaengine.so into process memory
4. JVM resolves native method symbols
5. Java calls native method → JNI bridge → C++ executes
6. C++ returns → JNI bridge → Java receives result
```

### 13.3 Memory Layout

```
┌─────────────────────────────────────┐
│  Java Heap (Dalvik/ART)             │
│  • Java objects, strings, arrays    │
│  • Garbage collected                │
├─────────────────────────────────────┤
│  Native Heap (C++)                  │
│  • CLIP_session, model weights      │
│  • SQLite DB handle                 │
│  • Manual malloc/new — YOU manage   │
├─────────────────────────────────────┤
│  JNI Reference Table (per thread)   │
│  • Local refs to Java objects       │
│  • Limited to ~512 entries          │
├─────────────────────────────────────┤
│  Code (libvantaengine.so)           │
│  • Read-only executable pages       │
└─────────────────────────────────────┘
```

---

## 14. Android-Specifics: Logging & `extern "C"`

### 14.1 Android Logging

```cpp
#include <android/log.h>

#define LOG_TAG "VantaEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Usage
LOGI("Models directory set to: %s", path.c_str());
LOGE("Failed to load CLIP model");
```

View with: `adb logcat -s VantaEngine:D`

### 14.2 `extern "C"` — Why It's Mandatory

```cpp
extern "C" JNIEXPORT void JNICALL Java_..._setModelsDirNative(...)
```

C++ compilers **mangle** function names (encode type info into the symbol). The JVM expects the exact C-style name. `extern "C"` tells the compiler:

> "Do not mangle this name. Export it exactly as written."

Without it, the JVM cannot find the symbol, and you get:
```
java.lang.UnsatisfiedLinkError: No implementation found for void ...
```

### 14.3 `JNIEXPORT` and `JNICALL`

| Macro | Purpose |
|-------|---------|
| `JNIEXPORT` | Ensures the symbol is exported in the `.so` (visibility) |
| `JNICALL` | Defines the calling convention (on some platforms, registers/stack layout differ) |

Always include both. They are no-ops on ARM64 but critical on x86/Windows.

---

## 15. Deep Dive: Two Real Functions from VantaEngine

### 15.1 Function 1: `setModelsDirNative` — The Simple Case

**Purpose:** Pass a file path from Java to C++ so the C++ engine knows where `.onnx` models live.

**Java Declaration:**
```java
package expo.modules.vantaengine;
public class VantaEngineModule {
    public native void setModelsDirNative(String modelsDir);
}
```

**C++ Implementation:**
```cpp
extern "C" JNIEXPORT void JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative(
    JNIEnv* env,
    jobject /* this */,      // Instance method → receives 'this'
    jstring modelsDir) {     // Java String → jstring

    // 1. Null check (Java could pass null)
    if (modelsDir == nullptr) return;

    // 2. Get C-string pointer from JVM
    //    'nullptr' means JNI will handle copying (not pinned buffer)
    const char* models_dir_cstr = env->GetStringUTFChars(modelsDir, nullptr);

    // 3. Copy into C++ std::string (safe, owned by us)
    std::string path(models_dir_cstr);

    // 4. Release the JNI string reference!
    //    The JVM can now garbage-collect or move the Java String.
    env->ReleaseStringUTFChars(modelsDir, models_dir_cstr);

    // 5. Use the C++ string
    VantaConfig::instance().set_models_dir(path);

    // 6. Log via Android logcat
    LOGI("Models directory set to: %s", path.c_str());
}
```

**What this teaches:**
- Basic JNI function signature
- `jstring` ↔ `const char*` conversion
- Memory hygiene (`ReleaseStringUTFChars`)
- `extern "C"` + `JNIEXPORT` + `JNICALL` boilerplate

---

### 15.2 Function 2: `startStoring` — The Complex Case

**Purpose:** Receive an array of `FileMeta` objects from Java, convert them to C++ `file_meta` structs, and insert into SQLite.

**Java Declaration:**
```java
public native boolean startStoring(String dbPath, FileMeta[] files);
```

**C++ Implementation (annotated):**

```cpp
extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_startStoring(
    JNIEnv* env,
    jobject /* this */,
    jstring dbPath,          // String
    jobjectArray files) {    // FileMeta[]

    // ── 1. Null checks ──
    if (dbPath == nullptr || files == nullptr) return JNI_FALSE;

    // ── 2. Convert dbPath jstring → std::string ──
    const char* db_path_cstr = env->GetStringUTFChars(dbPath, nullptr);
    std::string db_path(db_path_cstr);
    env->ReleaseStringUTFChars(dbPath, db_path_cstr);

    // ── 3. Get array length ──
    jsize count = env->GetArrayLength(files);
    LOGI("Received %d files to store", count);

    // ── 4. Prepare C++ vector ──
    std::vector<file_meta> file_list;
    file_list.reserve(count);

    // ── 5. Reflection: Find the Java class and cache field IDs ──
    jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
    if (fileMetaClass == nullptr) return JNI_FALSE;  // Class not found

    // GetFieldID(class, fieldName, signature)
    // Ljava/lang/String; = object reference to String
    jfieldID contentUriField = env->GetFieldID(fileMetaClass, "contentUri", "Ljava/lang/String;");
    jfieldID absPathField    = env->GetFieldID(fileMetaClass, "absPath",    "Ljava/lang/String;");
    jfieldID sizeBytesField  = env->GetFieldID(fileMetaClass, "sizeBytes",  "J");  // long
    jfieldID widthPxField    = env->GetFieldID(fileMetaClass, "widthPx",    "I");  // int
    // ... more fields ...

    // ── 6. Reflection: Find FileType enum class and .name() method ──
    jclass fileTypeClass = env->FindClass("expo/modules/vantaengine/FileType");
    jmethodID nameMethod = env->GetMethodID(fileTypeClass, "name", "()Ljava/lang/String;");

    // ── 7. Reflection: Find java.lang.Long for nullable Long? ──
    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID longValueMethod = env->GetMethodID(longClass, "longValue", "()J");

    // ── 8. Loop through Java array ──
    for (jsize i = 0; i < count; ++i) {
        // Get object at index i
        jobject fileObj = env->GetObjectArrayElement(files, i);
        if (fileObj == nullptr) continue;

        file_meta meta;  // C++ struct

        // ── 8a. Read String field: absPath ──
        jstring jAbsPath = (jstring)env->GetObjectField(fileObj, absPathField);
        if (jAbsPath != nullptr) {
            const char* absPathCstr = env->GetStringUTFChars(jAbsPath, nullptr);
            meta.content_uri = absPathCstr;  // std::string copies the bytes
            env->ReleaseStringUTFChars(jAbsPath, absPathCstr);
            env->DeleteLocalRef(jAbsPath);   // Free local ref!
        }

        // ── 8b. Read enum field: fileDtype (FileType) ──
        jobject jFileType = env->GetObjectField(fileObj, fileDtypeField);
        if (jFileType != nullptr) {
            // Call Java method: jFileType.name()
            jstring jFileTypeName = (jstring)env->CallObjectMethod(jFileType, nameMethod);
            const char* fileTypeNameCstr = env->GetStringUTFChars(jFileTypeName, nullptr);

            // Business logic: map "image" → "picture" for SQLite CHECK
            if (strcmp(fileTypeNameCstr, "image") == 0) {
                meta.file_type = "picture";
            } else {
                meta.file_type = fileTypeNameCstr;
            }

            env->ReleaseStringUTFChars(jFileTypeName, fileTypeNameCstr);
            env->DeleteLocalRef(jFileTypeName);
            env->DeleteLocalRef(jFileType);
        }

        // ── 8c. Read primitive fields (no release needed) ──
        meta.size_bytes = env->GetLongField(fileObj, sizeBytesField);
        meta.width_px   = env->GetIntField(fileObj, widthPxField);

        // ── 8d. Read nullable boxed Long? ──
        jobject jDurationMs = env->GetObjectField(fileObj, durationMsField);
        if (jDurationMs != nullptr) {
            // Unbox: ((Long)jDurationMs).longValue()
            meta.duration_ms = env->CallLongMethod(jDurationMs, longValueMethod);
            env->DeleteLocalRef(jDurationMs);
        } else {
            meta.duration_ms = 0;
        }

        // ── 8e. Push to C++ vector ──
        file_list.push_back(meta);

        // ── 8f. CRITICAL: Delete local ref to array element ──
        env->DeleteLocalRef(fileObj);
    }

    // ── 9. Call C++ business logic (no JNI here) ──
    bool success = insert_files(file_list, db_path);

    // ── 10. Return boolean to Java ──
    return success ? JNI_TRUE : JNI_FALSE;
}
```

**What this teaches:**
- Object arrays (`jobjectArray`)
- Reflection (`FindClass`, `GetFieldID`, `GetMethodID`)
- Reading primitives vs objects
- Calling Java methods from C++ (`CallObjectMethod`)
- Enum handling via `.name()`
- Nullable boxed types (`Long?`) unboxing
- **Memory discipline:** Every `GetObjectField`, `GetObjectArrayElement`, `CallObjectMethod` that returns an object creates a **local reference**. You must `DeleteLocalRef` them, especially in loops.

---

## 16. Common Pitfalls & Best Practices

### ❌ Pitfall 1: Forgetting `DeleteLocalRef` in Loops
```cpp
for (int i = 0; i < 1000; i++) {
    jstring s = env->NewStringUTF("x");
    // Use s...
    // MISSING: env->DeleteLocalRef(s);
}
// FATAL: local reference table overflow
```

### ❌ Pitfall 2: Using `GetStringUTFChars` Without `Release`
```cpp
const char* c = env->GetStringUTFChars(jstr, nullptr);
// ... never released → Java String pinned forever
```

### ❌ Pitfall 3: Storing `jobject` in Global C++ Variables
```cpp
static jobject g_cached_obj;  // DANGER: local ref becomes invalid!
static jobject g_cached_obj = env->NewGlobalRef(obj);  // Safe
```

### ❌ Pitfall 4: C++ Exceptions Crossing JNI Boundary
```cpp
try {
    risky_cpp_call();
} catch (const std::exception& e) {
    // Must handle here. If C++ exception escapes JNI function → CRASH.
    LOGE("Error: %s", e.what());
    return nullptr;
}
```

### ❌ Pitfall 5: Calling JNI from Unattached Threads
```cpp
std::thread([]() {
    env->FindClass("...");  // env is invalid! CRASH.
});
```

### ✅ Best Practice 1: Check for Nulls
Java can pass `null`. Always check `jobject` and `jstring` parameters.

### ✅ Best Practice 2: Cache Class/Method/Field IDs
`FindClass` and `GetMethodID` are expensive. Cache them in `static` locals or initialize once.

### ✅ Best Practice 3: Keep JNI Glue Thin
VantaEngine separates concerns:
- `vanta.cpp` = JNI conversion only
- `query_engine.cpp`, `CLIP_model.cpp` = pure C++ logic
- This allows unit testing C++ without the JVM

### ✅ Best Practice 4: Use RAII for JNI Resources
```cpp
class JStringGuard {
    JNIEnv* env;
    jstring js;
    const char* cs;
public:
    JStringGuard(JNIEnv* e, jstring s) : env(e), js(s) {
        cs = env->GetStringUTFChars(js, nullptr);
    }
    ~JStringGuard() { env->ReleaseStringUTFChars(js, cs); }
    const char* c_str() const { return cs; }
};

// Usage: auto-release guaranteed
JStringGuard guard(env, jstr);
std::string cpp(guard.c_str());
```

---

## 17. Quick Reference Cheat Sheet

### Function Template
```cpp
extern "C" JNIEXPORT <ReturnType> JNICALL
Java_<package>_<class>_<method>(JNIEnv* env, jobject thiz, ...) {
    // ...
}
```

### String Conversion
```cpp
// Java → C++
const char* c = env->GetStringUTFChars(jstr, nullptr);
std::string s(c);
env->ReleaseStringUTFChars(jstr, c);

// C++ → Java
return env->NewStringUTF(cpp_str.c_str());
```

### Array Loop
```cpp
jsize len = env->GetArrayLength(arr);
for (jsize i = 0; i < len; i++) {
    jobject elem = env->GetObjectArrayElement(arr, i);
    // ... use elem ...
    env->DeleteLocalRef(elem);
}
```

### Object Creation
```cpp
jclass cls = env->FindClass("java/util/HashMap");
jmethodID init = env->GetMethodID(cls, "<init>", "()V");
jobject obj = env->NewObject(cls, init);
```

### Method Call
```cpp
jmethodID mid = env->GetMethodID(cls, "name", "()Ljava/lang/String;");
jstring result = (jstring)env->CallObjectMethod(obj, mid);
```

### Thread Safety
```cpp
static std::mutex g_mutex;
{
    std::lock_guard<std::mutex> lock(g_mutex);
    // ... shared resource access ...
}
```

---

## Appendix: VantaEngine Architecture Map

```
Java Layer (Expo/React Native)
│
├─ VantaEngineModule.java          ← declares native methods
├─ IndexingService.java            ← background service calling JNI
└─ FileMeta.kt / FileType.kt       ← data classes passed to JNI
│
JNI Layer (vanta.cpp)
│
├─ setModelsDirNative()            ← config path
├─ startStoring()                  ← FileMeta[] → SQLite
├─ generateEmbeddingsNative()      ← CLIP + face pipeline
├─ searchImagesNative()            ← CLIP text search + NER
├─ getTopEntitiesNative()          ← face clustering results
└─ ... (metadata, neighbors, etc.)
│
C++ Business Layer
│
├─ CLIP_model.cpp / CLIP_tokenizer.cpp
├─ face_DB.cpp / face_pipeline.cpp
├─ query_engine.cpp (SQLite-vec KNN)
├─ ner.cpp (Named Entity Recognition)
├─ graph_db.cpp (co-occurrence graph)
└─ DBoperations.cpp (SQLite wrapper)
```

---

*Document generated from analysis of production VantaEngine JNI code.*
