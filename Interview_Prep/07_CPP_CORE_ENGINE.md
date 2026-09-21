# 07 — The C++ Core Engine

## What it is [VERIFIED-REPO]

`libvanta.so`, built from `modules/vanta-bridge/cpp/` by CMake 3.22.1 under the Android NDK, **C++20**,
**arm64-v8a only**.

| Module | Files | Lines | Role |
|---|---|---|---|
| JNI surface | `vanta.cpp` | 979 | 20 entry points, global session state, JSON assembly |
| Query | `query_engine.cpp/.hpp` | 949 | The search orchestrator |
| Query | `ner.cpp`, `bert_tokenizer.cpp` | 494 | BERT-tiny NER + WordPiece |
| Query | `IntentBuilder.cpp` | 232 | Hand-written text→caption mapping |
| Query | `typo_rectifyier.cpp` | 239 | SymSpell-style correction |
| Vision | `Seg_model.cpp` | 559 | SCRFD decode + NMS + alignment + ArcFace |
| Vision | `CLIP_model.cpp`, `CLIP_image.cpp`, `CLIP_tokenizer.cpp` | 451 | CLIP inference + preprocessing + BPE |
| Data | `DBoperations.cpp`, `clip_db.cpp`, `face_DB.cpp`, `graph_db.cpp` | 1,691 | Schema, vectors, faces, graph |
| Config | `Config.hpp`, `json_utils.hpp` | 153 | Mutex-guarded singleton, JSON escaping |

Third-party, compiled **into** the library: SQLite amalgamation (`sqlite3.c`) + `sqlite-vec.c`.
Linked as prebuilt shared: ONNX Runtime 1.26.0 (27 MB, aarch64, NDK r28, minimum Android 24).
Linked from the Android OpenCV SDK: `core`, `imgproc`, `imgcodecs`.

---

## Why C++?

### The honest, specific answer

**Do not say "C++ for performance."** That invites the question *"which operations actually require it?"* — and the
truthful answer is narrower and much more convincing:

**1. The dependencies are C/C++. This is the dominant reason.**

| Dependency | Reality |
|---|---|
| **ONNX Runtime** | Ships a C API and a C++ header wrapper. The Java/Kotlin binding exists (`onnxruntime-android`) but adds a JNI hop per inference and gives less control over tensor memory |
| **OpenCV** | Native library. The Java bindings are auto-generated JNI wrappers — every `Mat` operation crosses the boundary and `Mat` data lives natively anyway |
| **SQLite + sqlite-vec** | `sqlite-vec` is a C loadable extension. Android's `android.database.sqlite` **cannot load extensions** — `enableLoadExtension` is not exposed. To use `vec0` at all you must compile your own SQLite. **This alone forces native code.** |

So the real statement is: *"the vector database, the inference runtime and the image decoder are all C libraries, and
one of them (sqlite-vec) is literally unusable from the Android SQLite API. C++ is where those three meet."*

**2. Control over memory and copies in the hot loop.** Per image the engine does: decode → resize → normalise →
pack NCHW → FP32→FP16 convert → tensor. In C++ that's a handful of explicit buffers the engine owns and reuses
conceptually. On the JVM each intermediate is a GC-visible allocation, and a multi-minute indexing loop allocating
full-resolution `Bitmap`s and `float[]`s per image is exactly the workload that produces GC pauses and OOM.

**3. One core, many hosts.** The engine takes a DB path and a query string and returns JSON. Nothing in
`query_engine.cpp`, `face_DB.cpp` or `Seg_model.cpp` mentions JNI — only `vanta.cpp` does. That separation is real
and is what would make an iOS port a matter of writing a new ~979-line adapter rather than a new engine.

### Comparison

| Language | Verdict |
|---|---|
| **Kotlin/Java** | Would need `onnxruntime-android`, OpenCV Java bindings, and would make sqlite-vec **impossible**. Rejected on that last point alone |
| **Rust** | The genuinely interesting alternative: memory safety (would statically prevent the use-after-free on `g_analyzer`, and force handling of the `sqlite3_step` return codes this code ignores), `ort` and `rusqlite` crates are mature, `cxx`/`jni` crates give clean bindings. **Costs:** an extra toolchain in the NDK build, compiling sqlite-vec still means C interop, and a solo developer's velocity on a 3-week project. Honest verdict: *"if I started today and had Rust fluency, I'd seriously consider it — most of my real bugs in this repo are exactly the class Rust eliminates."* |
| **Swift** | Excellent on Apple platforms, a non-starter for Android |
| **Pure React Native / JS** | onnxruntime-web/WASM exists, but WASM SIMD on mobile is far slower than native ARM NEON, there's no threading story worth having, and SQLite-with-extensions in JS is not viable. Rejected |

### The honest costs of C++, which you should volunteer

| Cost | Concrete instance in this repo |
|---|---|
| **No memory safety** | `rebuild_query_engine_dictionary` does `delete g_analyzer; g_analyzer = nullptr;` while another search thread may be executing inside it → **use-after-free** |
| **Manual resource pairing** | Every `sqlite3_prepare_v2` needs `sqlite3_finalize`; every `GetStringUTFChars` needs `Release`. Vanta is disciplined about this but does it by hand; an RAII wrapper (`struct Stmt { ~Stmt(){sqlite3_finalize(s);} }`) would make it structural |
| **Silent error codes** | `sqlite3_step`'s result is discarded in ~15 places. That is precisely how the `entity_id = -1` FK failure ([01 §A2](./01_VANTA_REALITY_CHECK.md)) became invisible |
| **ABI/packaging** | `abiFilters 'arm64-v8a'` — no x86_64 means **no emulator support**, no armeabi-v7a means older devices are excluded. Adding them requires rebuilding ORT and OpenCV for each ABI and multiplies APK size |
| **Debugging** | A crash is a SIGSEGV tombstone, not a stack trace. `ndk-stack`/`addr2line` against unstripped `.so`s; no JS-level recovery |
| **Build complexity** | `find_package(OpenCV)` points at `thirdparty/opencv/android/sdk/native/jni`, which is **gitignored**. **A fresh clone does not build** — the OpenCV Android SDK must be downloaded and placed manually, and `thirdparty/README.md` is where that's documented. That's a genuine reproducibility gap |
| **C++ exceptions across JNI** | Would `std::terminate` the process. Coverage here is incidental (see [06](./06_JNI_BRIDGE.md)) |

---

## Build system details worth knowing

```cmake
set(CMAKE_CXX_STANDARD 20)
file(GLOB_RECURSE CLIP_SOURCES CONFIGURE_DEPENDS ".../Include/Preprocessing/CLIP/*.cpp")
add_library(vanta SHARED vanta.cpp ${DB_CODE} ${CLIP_SOURCES} ${SEGREGATION_SOURCES}
            ${QUERY_SOURCES} thirdparty/sqlite/sqlite3.c thirdparty/sqlite_vec/sqlite-vec.c)
```

Points an interviewer may push on:

- **`file(GLOB)` is generally discouraged** in CMake because new files don't trigger reconfiguration.
  `CONFIGURE_DEPENDS` mitigates it (the generator re-globs at build time) at a small cost. Vanta uses it
  deliberately and the comment says why: many small feature-grouped sources. Defensible.
- **`GRAPH_SOURCES` is set to `""`** with a comment: *"The Graph/ directory does not exist yet."* The graph lives in
  `DB/graph_db.cpp` instead. Harmless, but it tells you the graph layer was an afterthought that landed in the DB
  module.
- **sqlite-vec is compiled in, not loaded.** `#define SQLITE_CORE 1` before `#include "sqlite-vec.h"`, plus
  `sqlite3_auto_extension((void(*)())sqlite3_vec_init)` before every `sqlite3_open`. `SQLITE_CORE` makes the
  extension link statically against the amalgamation rather than go through `sqlite3_api_routines`. This is the
  correct way to ship sqlite-vec on Android and it's worth being able to explain.
- **Only `sqlite-vec.c` is compiled.** `sqlite-vec-diskann.c`, `sqlite-vec-ivf.c`, `sqlite-vec-ivf-kmeans.c` and
  `sqlite-vec-rescore.c` are all present in `thirdparty/` but are **not** in the `add_library` list. So Vanta has
  **only brute-force exhaustive KNN** — no ANN index — even though the ANN source is sitting in the repo. See
  [15](./15_SQLITE_AND_SQLITE_VEC.md).
- **No `-O` flag is set explicitly.** The Android Gradle Plugin supplies `-O2`/`-Oz` per build type; a Debug build
  compiles the engine at `-O0`, which would make indexing dramatically slower. Worth knowing if anyone asks why a
  debug build feels broken.
- **No NEON intrinsics, no `-ffast-math`, no explicit vectorisation.** `dot_product()` is a plain scalar loop:
  ```cpp
  static float dot_product(const float* a, const float* b, int dim) {
      float sum = 0.0f;
      for (int i = 0; i < dim; ++i) sum += a[i] * b[i];
      return sum;
  }
  ```
  At `-O2` clang will auto-vectorise this to NEON only if it may reassociate the float additions — which it may not
  without `-ffast-math`, because FP addition isn't associative. So this loop is very likely **scalar**. For 512 dims
  over a few hundred candidates that's irrelevant (~10⁵ FLOPs), but it's a precise, honest answer to "where would
  SIMD help?" — and the correct answer is *"not here; it would help in the image preprocessing loops, which are
  hand-written triple-nested `at<cv::Vec3f>()` accessors and are much hotter."*

---

## The preprocessing loops — a real optimisation target

```cpp
// Seg_model.cpp::get_faces — 640×640×3 = 1,228,800 iterations per image
for (int c = 0; c < 3; ++c)
  for (int h = 0; h < 640; ++h)
    for (int w = 0; w < 640; ++w)
      input_tensor_values[c*640*640 + h*640 + w] = float_img.at<cv::Vec3f>(h, w)[c];
```

`cv::Mat::at<T>()` performs a bounds check in debug and a multiply-add address computation always, and the access
pattern is **column-major over a row-major Mat** (stride 3 floats on the inner loop, jumping `c` on the outer) — i.e.
it strides badly through cache on every one of 1.2 M iterations.

The idiomatic fix is one call:

```cpp
cv::dnn::blobFromImage(padded, blob, 1.0/128.0, cv::Size(640,640),
                       cv::Scalar(127.5,127.5,127.5), /*swapRB=*/true, /*crop=*/false);
```

which does resize, scale, mean-subtract, **channel swap** and HWC→NCHW in one optimised, vectorised pass — and, as a
bonus, would have fixed the missing BGR→RGB conversion described in [10](./10_FACE_RECOGNITION.md).
The same pattern repeats in `CLIP_image.cpp` (though that one at least uses `cv::split` + `memcpy`, which is better).

---

## Concurrency: what's protected and what isn't

Covered in full in [03 §4](./03_SYSTEM_ARCHITECTURE.md). The summary for interview purposes:

**Safe:**
- `g_session_mutex` correctly guards creation and loading of the CLIP, face and NER sessions.
- `Ort::Session::Run` is documented thread-safe for concurrent calls on one session — so no lock is needed for
  inference, and the `Reads/01` note saying exactly that is correct.
- `std::atomic<int>` / `std::atomic<bool>` for progress counters and the pause flag.
- SQLite in WAL mode with one writer (the indexing thread, via `initialize_database`) and N read-only reader
  connections is a sound model.

**Unsafe — know all four:**

| Race | Mechanism | Severity |
|---|---|---|
| `g_tokenizer->load()` outside the mutex | Two concurrent searches mutate the same `unordered_map` | 🔴 UB, plausible crash on first two rapid searches |
| `delete g_analyzer` in `rebuild_query_engine_dictionary` | Renaming an entity frees an object a concurrent search may be inside | 🔴 Use-after-free |
| `g_index_status` / `g_current_processing_file` (`std::string`) | Written by the indexer, read by `getIndexProgressNative` on the JS-triggered thread | 🟠 Torn read / UAF on SSO→heap reallocation |
| `g_owner_entity_id` (plain `int64_t`) | Written by `setOwnerEntityIdNative`, read by search | 🟢 Formally a race; aligned 64-bit loads on aarch64 make tearing implausible |

**No `busy_timeout` is set on any connection.** If `resetFaceData` and `generateEmbeddings` run concurrently — both
reachable from the UI — the second writer gets `SQLITE_BUSY` immediately rather than waiting. One line
(`sqlite3_busy_timeout(db, 5000)`) fixes it. See [18](./18_FAILURE_MODES_AND_CONSISTENCY.md).

---

## Cross-platform reality

```
      ┌─────────────────────────────────────────┐
      │  React Native + TypeScript UI           │  ← genuinely portable, exists
      ├─────────────────────────────────────────┤
      │  Platform adapter                       │
      │   Android: Kotlin, 1200 lines  ✅       │
      │   iOS:     Swift, 15 lines     ❌ stub  │
      ├─────────────────────────────────────────┤
      │  Shared C++ core, ~4500 lines           │  ← portable in principle, compiled only for Android
      └─────────────────────────────────────────┘
```

| Layer | Portable? | Why |
|---|---|---|
| Image preprocessing, CLIP, SCRFD/ArcFace, clustering, graph, retrieval, ranking, SQL | **Yes, in principle** | Pure C++ + ORT + OpenCV + SQLite, all of which build for iOS. Nothing in these files is Android-specific *except* `#include <android/log.h>` and the `__android_log_print` macros, which appear in ~10 files and would need a one-macro abstraction |
| JNI adapter (`vanta.cpp`) | **No** | Would be replaced by Swift/ObjC++ calling the same engine functions |
| Media discovery | **No** | MediaStore ↔ `PHPhotoLibrary`. Completely different models — Photos on iOS returns `PHAsset`s, not paths, which would *force* the bytes-over-the-boundary design Vanta should already be using |
| Permissions | **No** | `READ_MEDIA_IMAGES` ↔ `NSPhotoLibraryUsageDescription` |
| Background work | **No** | Foreground `Service` ↔ `BGProcessingTask` (which is far more restrictive — iOS will not let you burn an hour of CPU on demand) |
| Model packaging | **No** | APK assets → `filesDir` ↔ app bundle resources |

> **What to say:** *"The C++ core is written to be portable and has no JNI in it outside the adapter file, but I have
> only built and shipped it for Android arm64. iOS is a 15-line stub. I'd describe the architecture as
> 'portable by construction, single-platform in practice' — and I'd flag that the iOS port's hardest part isn't the
> engine, it's that `PHPhotoLibrary` gives you assets rather than file paths, which breaks the path-based file access
> the engine currently assumes."*
