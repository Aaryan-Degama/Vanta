# 06 — The JNI Bridge

## Concepts, then Vanta's actual code

### The vocabulary

| Term | What it is | Where it matters in Vanta |
|---|---|---|
| **JNI** | The C ABI by which native code calls into, and is called from, a JVM | The only way Kotlin can reach `libvanta.so` |
| **JVM / ART** | On Android, ART is the runtime | Owns all Java objects the native code touches |
| **`JNIEnv*`** | Per-thread interface pointer — a table of ~230 function pointers. **Thread-local. Never cache across threads.** | First parameter of all 20 entry points |
| **`JavaVM*`** | Process-wide handle; the only JNI pointer that *is* shareable across threads | **Vanta never uses it** — it never calls back into Java, so it never needs `AttachCurrentThread` |
| **`JNI_OnLoad`** | Optional hook at `System.loadLibrary` time, used for `RegisterNatives` and to stash the `JavaVM*` | **Vanta does not define it** — it uses name-mangled discovery instead |
| **Local reference** | Default lifetime for any `jobject` returned by JNI. Freed automatically when the native method returns. The table is small (guaranteed ≥16, typically 512 on ART) | Critical in `startStoring`, which touches N × ~5 objects |
| **Global reference** | `NewGlobalRef` — survives across calls, must be `DeleteGlobalRef`'d | **Vanta uses none** |

### How Vanta's methods get found

`VantaEngineModule.kt` declares:

```kotlin
private external fun searchImagesNative(dbPath: String, query: String): String
```

and `vanta.cpp` defines:

```cpp
extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_searchImagesNative(
    JNIEnv* env, jobject /*this*/, jstring dbPath, jstring queryStr)
```

The mangling rule is `Java_` + fully-qualified class with `.`→`_` + `_` + method name. `extern "C"` is mandatory —
without it C++ name mangling produces a symbol ART cannot find, and you get
`UnsatisfiedLinkError: No implementation found for…` at first call, not at load time.

**Why this matters (a likely interview question):** the alternative is `RegisterNatives` inside `JNI_OnLoad`, which
(a) is faster to resolve, (b) lets you use arbitrary C++ function names, (c) fails loudly at library-load time rather
than at first invocation, and (d) is the standard way to hand-register a stripped/obfuscated build. Vanta uses
discovery because it's simpler and the method count is small. That's a defensible trade — just know the alternative.

### A quirk worth being able to explain

```cpp
extern "C" JNIEXPORT jboolean JNICALL
Java_expo_modules_vantaengine_IndexingService_setModelsDirNative(JNIEnv* env, jobject, jstring modelsDir) {
    Java_expo_modules_vantaengine_VantaEngineModule_setModelsDirNative(env, nullptr, modelsDir);
    return JNI_TRUE;
}
```

Because `IndexingService` declares its **own** `external fun setModelsDirNative`, JNI looks for a symbol mangled with
`IndexingService`, not `VantaEngineModule`. So `vanta.cpp` defines thin forwarding shims for all three of the
Service's natives. Note it passes `nullptr` for the `jobject this` — safe *only* because none of these functions
dereference it. If a future version used `this`, it would crash. A cleaner design registers one implementation
against both class names via `RegisterNatives`.

---

## Type mapping used in Vanta [VERIFIED-REPO]

| Kotlin | JNI type | Conversion in `vanta.cpp` | Notes |
|---|---|---|---|
| `String` | `jstring` | `GetStringUTFChars` → `std::string` → `ReleaseStringUTFChars` | **Copies.** UTF-8 (technically *modified* UTF-8 — differs for U+0000 and supplementary planes) |
| `Long` | `jlong` | direct | `entityId` |
| `Int` | `jint` | direct | |
| `Boolean` | `jboolean` | `JNI_TRUE`/`JNI_FALSE` | `jboolean` is `uint8_t`, not `bool` |
| `Array<FileMeta>` | `jobjectArray` | `GetArrayLength` + `GetObjectArrayElement` per index | See below |
| `FileType` (enum) | `jobject` | `GetMethodID(cls,"name","()Ljava/lang/String;")` + `CallObjectMethod` | |
| `Long?` (nullable) | `jobject` (`java.lang.Long`) | null-check, then `CallLongMethod(obj, longValue)` | Correct handling of Kotlin nullable boxing |
| return `Array<HashMap<String,Any>>` | `jobjectArray` | `NewObjectArray` + `NewObject(HashMap)` + `CallObjectMethod(put)` | Only used by the debug `getStoredFilesNative` |
| return JSON | `jstring` | `env->NewStringUTF(json.c_str())` | **The pattern used by 11 of the 20 entry points** |

### Reading one `FileMeta` — the actual code

```cpp
jclass fileMetaClass = env->FindClass("expo/modules/vantaengine/FileMeta");
jfieldID absPathField = env->GetFieldID(fileMetaClass, "absPath", "Ljava/lang/String;");
// ... 11 more GetFieldID calls, hoisted OUT of the loop  ✅
for (jsize i = 0; i < count; ++i) {
    jobject fileObj = env->GetObjectArrayElement(files, i);
    jstring jAbsPath = (jstring)env->GetObjectField(fileObj, absPathField);
    const char* c = env->GetStringUTFChars(jAbsPath, nullptr);
    meta.content_uri = c;                       // copy #1: into std::string
    env->ReleaseStringUTFChars(jAbsPath, c);
    env->DeleteLocalRef(jAbsPath);              // ✅ essential
    ...
    env->DeleteLocalRef(fileObj);               // ✅ essential
}
```

**What this code gets right:**
- `GetFieldID` / `GetMethodID` are called **once**, outside the loop. Field-ID lookup is a hash-map probe on a UTF-8
  descriptor string; doing it per row would dominate the loop.
- Every `jstring` and every `jobject` is `DeleteLocalRef`'d. Without this, a 100k-file scan would overflow the local
  reference table (~512 entries) and abort the VM with
  `JNI ERROR (app bug): local reference table overflow`. This is the #1 JNI bug and Vanta avoids it.
- `ReleaseStringUTFChars` is always paired with `GetStringUTFChars`. Unbalanced pairs leak native memory.

**What it gets wrong / could do better:**
- `FindClass` returns a **local** ref that is never deleted (3 of them per call) — harmless, since it's 3 refs per
  call, not per row.
- No exception checks. `FindClass`, `GetFieldID` and `CallObjectMethod` can all raise a pending Java exception. Only
  `FindClass` is null-checked. If a pending exception exists and you keep calling JNI functions, behaviour is
  undefined. The disciplined pattern is `if (env->ExceptionCheck()) { env->ExceptionClear(); return JNI_FALSE; }`.
- `meta.display_name` is never populated — the `displayName` field is on the Kotlin class but has no `GetFieldID`.
  That is why every search result returns `display_name: ""`. *(See [01 §B3](./01_VANTA_REALITY_CHECK.md).)*

---

## Trace 1 — Search

```
SearchScreen.tsx
  searchImages(query)                          modules/vanta-bridge/index.ts
  → VantaEngineModule.searchImages(query)      src/VantaEngineModule.ts (requireNativeModule)
  → AsyncFunction("searchImages")              VantaEngineModule.kt:303
      Thread { extractAssetsIfNeeded(ctx)
               setModelsDirNative(...)         ← JNI crossing (cheap, idempotent)
               setCropsDirNative(...)          ← JNI crossing
               searchImagesNative(dbPath, q)   ← JNI crossing  ★ the real one
               promise.resolve(result) }
  → Java_..._searchImagesNative                vanta.cpp:476
      GetStringUTFChars × 2
      lock(g_session_mutex): ensure g_clip_session loaded
      (UNLOCKED) ensure g_tokenizer loaded     ← the race, see 01 §C3
      lock(g_session_mutex): ensure g_ner_model loaded (non-fatal if it fails)
      search_images(db_path, query, g_clip_session, g_tokenizer, ner_ptr, g_owner_entity_id)
        ├ get_corrected_query()                typo_rectifyier.cpp
        ├ ner_model->run()                     ner.cpp  (BERT-tiny, seq len 32)
        ├ get_all_person_entities()            graph_db.cpp
        ├ resolve_span_to_entity_id()          query_engine.cpp (Damerau-Levenshtein)
        ├ IntentBuilder::buildIntent()         IntentBuilder.cpp
        ├ tokenizer->encode()                  CLIP_tokenizer.cpp (BPE, 77 tokens)
        ├ clip_session->get_text_embedding()   CLIP_model.cpp (ORT, FP16 → FP32, L2-normalised)
        ├ SQL: entity_memberships / entity_relation_files / clip_vec MATCH
        └ dot_product() + sort + verify_entities_in_file() + burst dedup
      build JSON string
      env->NewStringUTF(json)                  ← one allocation, one copy
  → Kotlin String → Promise → JS → JSON.parse
```

**JNI crossings per search: 3** (two of them trivial setters). Payload: 2 strings in, 1 string out.

## Trace 2 — Indexing

```
SettingsScreen → generateEmbeddings()
  → AsyncFunction("generateEmbeddings")
      startForegroundService(Intent(ACTION_START, dbPath))
  → IndexingService.onStartCommand → Thread {
        extractAssetsIfNeeded()                     APK assets → filesDir (once)
        setModelsDirNative / setCropsDirNative      ← 2 JNI crossings
        generateEmbeddingsNative(dbPath)            ← 1 JNI crossing, runs for MINUTES
    }
  → Java_..._IndexingService_generateEmbeddingsNative  (shim)
  → Java_..._VantaEngineModule_generateEmbeddingsNative   vanta.cpp:306
      sqlite3_auto_extension(sqlite3_vec_init)
      initialize_database()  → files/face/graph DDL, WAL, foreign_keys=ON
      init_clip_db()         → clip_vec vec0 table
      lock(g_session_mutex): load CLIP + face models
      while (!g_pause_embedding):
         get_unindexed_images(db_path, 10)          ← own read-only connection, per batch
         for each image:
            CLIP_instance(path)                     cv::imread + resize + normalise   ← DECODE #1
            get_embedding()                         ORT FP16
            save_clip_embedding()                   clip_vec insert + status='indexed'
            run_face_pipeline()                     ifstream + cv::imdecode           ← DECODE #2
               get_faces()                          SCRFD 640×640 + NMS
               get_embedding()                      align 112×112 + ArcFace, per face
               align_face(…, 320) + cv::imwrite     FaceCrops/{id}_{i}.jpg
               save_face_detection / save_face_embedding
            cluster_faces_for_file()                person_centroids MATCH k=1, EMA update
            update_graph_for_file()                 pairwise co-occurrence upsert
      recluster_pending_faces()
      merge_similar_entities()
  (Kotlin polls getIndexProgressNative() — 1 crossing per poll, returns ~100-byte JSON)
```

**JNI crossings for indexing 50,000 photos: 3, plus one per progress poll.** Not 50,000.

---

## JNI performance — the analysis an interviewer wants

### What a JNI crossing actually costs

A JNI call is **not** a syscall. For a statically-linked (discovered) native method, ART performs: a stub transition
that saves callee-saved registers and sets up the managed→native frame, a thread-state transition to
`kNative` (so the GC knows this thread won't touch managed objects), the call, then the reverse, including a
suspend-point check. **[ESTIMATED]** on modern arm64 this is on the order of tens of nanoseconds for a no-arg call.

What is genuinely expensive is not the transition but:

| Operation | Why it costs |
|---|---|
| `GetStringUTFChars` | Allocates and encodes a modified-UTF-8 copy of the whole string |
| `NewStringUTF` | Decodes UTF-8 and allocates a Java `String` in the managed heap |
| `GetObjectField` / `CallObjectMethod` | Each returns a **new local reference** (table slot + bookkeeping) |
| `FindClass` / `GetFieldID` | String-descriptor lookup in the class's field table |
| Local-ref table pressure | Overflow ⇒ VM abort |

### Why "one JNI call per image" would be bad — and why Vanta doesn't do it

If the design were `for (photo in photos) { embedNative(photo) }`, for 50k photos you'd pay 50k transitions *plus* 50k
string conversions *plus*, far worse, you'd have to **reload or keep alive the ONNX sessions and the SQLite
connection across calls** and you'd be re-entering/leaving the managed world in the middle of a tight compute loop.
The transitions themselves would be ~milliseconds in total — irrelevant. The real damage is architectural: you cannot
hold a transaction open, you cannot batch inserts, and you cannot keep warm native state without globals.

**Vanta's actual design inverts this correctly:** the JNI boundary is crossed once per *operation*, and the entire
loop, all inference, all DB work and all clustering live on the native side. That is the right answer and you should
say it confidently.

### Where Vanta's JNI design is genuinely weak

`startStoring` is one call — good — but the payload is the whole scan:

**[ESTIMATED]** for 100,000 files:
- Kotlin holds `List<FileMeta>` — 14 fields, ~4 `String` objects each. At a conservative ~250 bytes/entry including
  string data, that is **~25 MB of JVM heap** materialised before the call, on a device where the default per-app
  heap limit is often 256–512 MB.
- C++ then performs ~12 `GetObjectField` per row = **1.2 M** accessor calls, of which ~4 per row are
  `GetStringUTFChars`/`ReleaseStringUTFChars` pairs = **~400 k string conversions**, plus **~500 k**
  `NewLocalRef`/`DeleteLocalRef` cycles.

At an **[ESTIMATED]** ~100 ns per accessor pair this is on the order of **0.1–0.5 s** — tolerable, but it is a
peak-memory hazard and it throws away the batching that `scanPhone(batchSize = 200)` already implements.

### Zero-copy and batching options, ranked

| Option | Mechanism | Gain | Cost |
|---|---|---|---|
| **1. Actually use the 200-batch callback** (best ROI) | Call `startStoring(dbPath, batch.toTypedArray())` from inside `onBatch` | Caps JVM peak at 200 entries (~50 KB) instead of 25 MB; 500 crossings instead of 1 — still negligible | 3 lines. **Do this.** |
| **2. Columnar primitive arrays** | Pass `LongArray(sizes)`, `LongArray(mtimes)`, `IntArray(w)`, `IntArray(h)` + one `Array<String>` | `GetLongArrayElements`/`GetPrimitiveArrayCritical` can hand back a **direct pointer** with no copy | Loses the nice `FileMeta` type; strings still need conversion |
| **3. `java.nio.ByteBuffer.allocateDirect` + a packed binary layout** | `GetDirectBufferAddress` → a raw `void*` into off-heap memory. **True zero-copy** | Eliminates all per-field JNI and all string copies | You hand-write a serialisation format; strings need explicit length-prefixing |
| **4. Move the scan into C++** | Not possible — MediaStore is a `ContentProvider`, reachable only through the JVM | — | — |

**For image bytes specifically** (the scoped-storage fix from [05](./05_KOTLIN_ANDROID.md)): pass the file
descriptor from `ContentResolver.openFileDescriptor(uri,"r").detachFd()` as a `jint` and `fdopen`/`mmap` it natively.
That is zero-copy *and* scoped-storage-correct, and it's strictly better than passing a `ByteArray` (which costs a
full copy of every JPEG through the JVM heap).

---

## Exceptions, errors, and lifetimes

| Concern | Vanta's behaviour |
|---|---|
| C++ exception escaping into the JVM | **Would terminate the process.** `generateEmbeddingsNative` wraps the per-image body in `try/catch(const std::exception&)`; `searchImagesNative` wraps `get_text_embedding`. `run_face_pipeline` → `get_embedding` → `align_face` can throw from `cv::warpAffine` if `estimateAffinePartial2D` returns an empty matrix — that path is caught only by the outer per-image handler in `generateEmbeddingsNative`. Acceptable, but the coverage is incidental rather than designed |
| Java exception raised by a JNI call | **Never checked.** No `ExceptionCheck`/`ExceptionClear` anywhere |
| Error signalling to JS | Sentinel values: `"[]"`, `"{}"`, `JNI_FALSE`. **No exception is ever thrown to Kotlin**, so a failed search and an empty search are indistinguishable in the UI |
| Native crash | SIGSEGV in `libvanta.so` kills the whole app — no JS-level recovery. `ErrorBoundary.tsx` only catches React render errors |
| Global lifetimes | `g_clip_session`, `g_face_session`, `g_tokenizer`, `g_ner_model` are `new`'d and **never deleted**. Intentional process-lifetime leak; `unload()` exists on both model classes and is never called. Means models stay resident after indexing finishes — see [17](./17_PERFORMANCE_AND_SCALABILITY.md) |

> **The improvement to propose:** a structured error envelope — `{"ok":false,"code":"MODEL_LOAD_FAILED","message":…}`
> — instead of `"[]"`. The UI currently cannot tell the user "the NER model failed to load, search is degraded",
> even though the engine knows exactly that and logs it.
