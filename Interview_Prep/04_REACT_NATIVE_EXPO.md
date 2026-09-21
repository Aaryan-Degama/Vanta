# 04 — React Native & Expo

## What Vanta actually uses [VERIFIED-REPO]

| Item | Version | Source |
|---|---|---|
| Expo SDK | `~56.0.9` | `package.json` |
| React Native | `0.85.3` | `package.json` |
| React | `19.2.3` | `package.json` |
| Navigation | `@react-navigation/native` 7.x + native-stack | `package.json` |
| TypeScript | `~6.0.3`, `strict` via tsconfig | `package.json` |
| Native module API | **Expo Modules API** (`expo.modules.kotlin.modules.Module`) | `VantaEngineModule.kt` |
| Screens | 6: Search, People, EntityDetail, Settings, AdvancedOption, Debug | `App.tsx` |

The bridge surface is 20 `AsyncFunction`s. **Every one returns either a primitive or a JSON string** — there are no
Expo `Record` types crossing the boundary.

---

## Why React Native?

Be precise about the actual reason, because the naive answer ("cross-platform") is **falsifiable in this repo** — iOS
is a 15-line stub.

The honest chain:

```
Requirement:  a gallery-grade UI — grid, search-as-you-type, people page, detail sheets, theming, navigation
Constraint:   solo developer, ~3.5 weeks of calendar time (2026-06-10 → 2026-07-03, 53 commits)
Observation:  ~0% of the compute budget is in the UI. 100% is behind JNI.
      ↓
Decision:     spend developer time where it's scarce — UI iteration speed — and buy it with a
              runtime overhead that is provably irrelevant for this workload.
```

**The load-bearing argument:** Vanta's UI does three things — render an image grid, collect a string, poll a progress
counter. None of that is frame-budget sensitive. The expensive work (CLIP, SCRFD, ArcFace, SQLite, clustering) runs in
C++ and is 10²–10³× more expensive than any bridge hop. So React Native's known weakness (JS↔native marshalling and a
JS-thread render loop) sits on the part of the system that costs nothing, and its strength (fast UI iteration,
hot reload, one component model) sits on the part where the developer's time actually went.

If the UI *were* the bottleneck — a 60fps scrubbing timeline, real-time camera overlay, gesture-driven photo editing —
this trade would be wrong and I'd say so.

---

## Alternatives, honestly compared

| Option | What you gain | What you lose | Verdict for Vanta |
|---|---|---|---|
| **Native Android (Views/XML)** | No bridge at all; direct `Bitmap`/`ImageDecoder`; best memory control for image grids | Slowest UI iteration; imperative view code; no path to iOS at all | Rejected — the bridge wasn't the bottleneck |
| **Jetpack Compose** | Modern declarative Kotlin UI, **same language as the Android layer**, zero JNI-adjacent marshalling, best-in-class Android image handling (Coil) | Android-only (Compose Multiplatform iOS was still maturing); new learning curve | **The strongest counterfactual.** Given the project is Android-only anyway, Compose would have removed an entire runtime and language from the stack. My honest answer: *"if I'd known at the start that iOS would stay a stub, Compose was the better choice."* |
| **Flutter** | Excellent, consistent rendering via Skia/Impeller; genuinely good cross-platform | Dart FFI to C++ is fine, but the **Android-specific** work (MediaStore, foreground Service, scoped storage) still needs platform channels into Kotlin — so you keep the Kotlin layer *and* add Dart. Larger baseline binary | Rejected — adds a language without removing one |
| **Kotlin Multiplatform** | Share *business logic* in Kotlin; native UI per platform; clean interop with the JVM layer | Doesn't help here: Vanta's shared logic is already in **C++**, not Kotlin. KMP would share the thin part and leave the thick part untouched | Rejected — solves a problem Vanta doesn't have |
| **SwiftUI / fully native both platforms** | Best possible UX on each | 2× UI implementations; I have no Apple hardware in this project's loop | Rejected |
| **Web / PWA** | Zero install | Fatal: no MediaStore, no filesystem access to the camera roll, no ONNX Runtime native EPs, no background service, no JNI. `VantaEngineModule.web.ts` exists purely so `expo start --web` renders the UI shell | Impossible by construction |

### "Why didn't you build the UI entirely natively?"

> "Because none of Vanta's cost is in the UI. The UI issues one string and renders a grid of file paths; everything
> expensive happens in C++ behind a single JNI call. Choosing RN bought me fast iteration on the part of the system
> that changed most often — I rewrote the search screen and people screen several times — at a cost I can bound:
> one string in, one JSON string out, once per user action. That said, since the project ended up Android-only,
> Jetpack Compose would have been the cleaner answer, because it would have removed a whole runtime rather than
> justified it."

### "Why is React Native appropriate when the backend is native C++?"

> "They're solving different problems and the boundary between them is narrow and cheap. RN owns presentation; C++
> owns compute. The interface is 20 async calls, each a single crossing with a string payload. There is no chatty
> per-item traffic — I don't cross the bridge per image, I cross it per *operation*. That's what makes the
> combination viable: the bridge cost is O(1) per user action, not O(n) per photo."

---

## Why Expo?

### What Expo actually provides here [VERIFIED-REPO]

1. **The Expo Modules API** — this is the real reason. Compare:

   ```kotlin
   // Expo Modules API — what Vanta uses
   AsyncFunction("searchImages") { query: String, promise: Promise -> ... }
   ```

   vs. classic RN native modules, which need `ReactContextBaseJavaModule`, `@ReactMethod`, manual
   `ReadableMap`/`WritableMap` conversion, a `ReactPackage`, and registration in `MainApplication`. The Expo
   version gives typed Kotlin parameters, automatic coroutine/Promise plumbing, and lifecycle hooks (`OnCreate`,
   used here to extract model assets and restore the owner entity) for a fraction of the code.

2. **Autolinking** — `expo-module.config.json` is the entire registration:
   ```json
   { "platforms": ["apple","android"],
     "android": { "modules": ["expo.modules.vantaengine.VantaEngineModule"] } }
   ```

3. **Prebuild** — `npx expo run:android` generates `android/` from `app.json`, wires the Gradle plugin
   (`expo-module-gradle-plugin`), and drives CMake/NDK. Vanta gitignores `vanta_app/android` precisely because it's
   generated.

4. **Batteries** — `expo-blur`, `expo-linear-gradient`, `expo-status-bar`, `@expo/vector-icons`, `jest-expo`.

### Managed vs. prebuild — and why Vanta cannot be managed

| Workflow | Native code? | Vanta compatible? |
|---|---|---|
| **Managed / Expo Go** | No. You get the prebuilt Expo Go binary | ❌ **Impossible.** Vanta needs a custom `.so` (`libvanta.so`), the NDK, CMake, a bundled ONNX Runtime, OpenCV, and a foreground Service |
| **Prebuild / bare** | Yes — `android/` is generated, then compiled from source | ✅ **This is what Vanta uses** |

**The clean formulation:** *"Expo isn't a sandbox you're trapped in, it's a toolchain. The prebuild workflow gives you
Expo's module system, autolinking and config plugins while leaving you full native access — which Vanta needs,
because half the project is a C++ shared library."*

### What Expo cannot do for Vanta

- It cannot bundle a 27 MB ONNX Runtime `.so` or drive CMake — that's `build.gradle` + `CMakeLists.txt`.
- It cannot express MediaStore queries, scoped storage, or a foreground Service — that's Kotlin.
- It cannot marshal an `Array<FileMeta>` into C++ — that's hand-written JNI.
- Expo Go is unusable, so every change to native code requires a full rebuild (~5–10 min first time).

### "Why not plain React Native?"

> "Plain RN works, but the Expo Modules API is strictly less boilerplate for the same result, and autolinking plus
> prebuild removes an entire class of manual Gradle/Podfile wiring. Since Expo's prebuild workflow imposes no
> restriction on native code — I'm shipping a custom C++ `.so` and a foreground service through it — there was no
> cost to pay for that convenience."

### "Why not Flutter?"

> "Flutter's rendering is genuinely better, but it wouldn't have removed any work here. All of Vanta's
> platform-specific code is Android-specific — MediaStore, scoped storage, foreground services, JNI — so I'd still
> write the identical Kotlin layer and then add Dart and platform channels on top. Flutter pays off when the UI is
> the hard part. In Vanta the UI is the easy part."

---

## Honest weaknesses of the RN choice as implemented

| Issue | Evidence | Impact |
|---|---|---|
| **Untyped JSON boundary** | `SearchScreen.tsx` expects `size`, `date_taken`; C++ emits `size_bytes`, `mtime_unix` | Silent `undefined`. A typed codegen or a shared schema would have caught it |
| **`display_name` always `""`** | JNI never reads `FileMeta.displayName` | The UI can't show filenames; it falls back to the image itself |
| **Image loading is `file://` + `abs_path`** | `<Image source={{uri:'file://'+abs_path}}/>` | Couples the UI to legacy absolute paths, which scoped storage can hide. A content-URI path would be correct — see [05](./05_KOTLIN_ANDROID.md) |
| **No search debounce or cancellation** | `SearchScreen` calls `searchImages` directly | Every call spawns a raw Kotlin `Thread`. Fast typing ⇒ unbounded threads ⇒ see [01 §C3](./01_VANTA_REALITY_CHECK.md) |
| **Progress by polling** | `getIndexProgress()` on a JS timer | Simpler than Expo `EventEmitter`, but wakes the JS thread on a fixed cadence during a multi-minute job |
| **Dead exported API** | `helloFromKotlin` / `helloFromCpp` exported from `index.ts`, not implemented in Kotlin | Would throw if called |
