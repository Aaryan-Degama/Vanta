# Vanta App — Project Context

> This file is a living reference for AI assistants working on the project.
> Last updated: 2026-06-30

## 1. What this project is

Vanta is a **local-first, on-device AI media search app** for Android (with iOS
scaffolding). It scans the phone's MediaStore, indexes images/videos/audio/documents
into an SQLite database with vector embeddings, and lets the user search their
photos with natural-language queries using CLIP. It also runs face recognition /
clustering to build a "People" view.

- **Front end**: React Native / Expo (TypeScript)
- **Native bridge**: Expo module (`modules/vanta-bridge`) written in Kotlin + JNI + C++
- **Inference**: ONNX Runtime + OpenCV on device
- **Models used**:
  - CLIP ViT-B/32 (image + text encoders) as FP16 ONNX
  - buffalo_sc / InsightFace-style face detection + recognition (`det_500m.onnx`, `w600k_mbf.onnx`)
  - NER/BERT for query intent (kept in tree but excluded from the build)
- **Database**: SQLite + sqlite-vec for vector search

## 2. Repo layout

```text
vanta_app/
├── App.tsx                    # Root navigator wrapped in error boundary + theme provider
├── ThemeContext.tsx           # Light/dark theme context
├── ErrorBoundary.tsx          # Global JS error boundary
├── hooks/useVantaError.ts     # Consistent native error handler
├── utils/formatting.ts        # Pure helpers (formatBytes, formatDate)
├── index.ts                   # Expo root registration
├── app.json                   # Expo config
├── package.json               # RN deps (Expo 56, RN 0.85, React 19)
├── .github/workflows/ci.yml   # GitHub Actions CI
├── screens/                   # Top-level RN screens
│   ├── SearchScreen.tsx       # Main CLIP search
│   ├── SettingsScreen.tsx     # Index trigger + stats + dark mode
│   ├── DebugScreen.tsx        # Raw DB entries / scan test
│   ├── PeopleScreen.tsx       # Face-cluster grid
│   └── EntityDetailScreen.tsx # Person detail: photos + co-occurrences
├── modules/vanta-bridge/      # Expo native module
│   ├── index.ts               # Public JS API
│   ├── src/VantaEngineModule.ts
│   ├── src/VantaEngine.types.ts
│   ├── android/               # Kotlin + CMake JNI build
│   │   ├── build.gradle
│   │   └── src/main/java/expo/modules/vantaengine/
│   │       ├── VantaEngineModule.kt   # JS ↔ JNI bridge
│   │       ├── VantaEngineConfig.kt   # Centralized model/db paths
│   │       ├── Android_scan.kt        # MediaStore scanner
│   │       └── IndexingService.kt     # Foreground-service indexer
│   └── cpp/                   # C++ engine
│       ├── vanta.cpp          # JNI entry points
│       ├── Config.hpp         # Runtime model-path singleton
│       ├── json_utils.hpp     # Safe JSON string helpers
│       ├── CMakeLists.txt
│       ├── DB/                # SQLite schema + operations
│       ├── Include/Preprocessing/CLIP/        # CLIP tokenizer/image/model
│       ├── Include/Preprocessing/Segregation/ # Face detection/embedding
│       ├── Include/Query/Query_processing/    # Search + typo correction
│       └── thirdparty/        # onnxruntime, opencv, sqlite, sqlite_vec
└── assets/                    # App icons + model assets
```

## 3. High-level data flow

1. `SettingsScreen` calls `startStoring()`.
2. Kotlin (`VantaEngineModule`) requests media permissions, scans MediaStore,
   batches files, and passes them over JNI to `vanta.cpp`.
3. C++ inserts file metadata into SQLite.
4. User taps "Start Indexing" → `generateEmbeddings()` launches
   `IndexingService` foreground service.
5. C++ loads CLIP + face models, loops over unindexed images, saves CLIP
   embeddings to `clip_vec`, detects/embeds/cluster faces, then builds a
   co-occurrence graph.
6. `SearchScreen` calls `searchImages(query)` → C++ tokenizes text, runs CLIP
   text model, queries sqlite-vec for nearest images, returns JSON.
7. `PeopleScreen` / `EntityDetailScreen` read face clusters + co-occurrence
   graph via `VantaEngine` module.

## 4. Native module API surface

From `modules/vanta-bridge/index.ts`:

| Function               | Purpose                                                     |
| ---------------------- | ----------------------------------------------------------- |
| `startStoring()`       | Scan MediaStore and insert metadata                         |
| `getStoredFiles()`     | Debug: list DB rows                                         |
| `generateEmbeddings()` | Start foreground-service indexing                           |
| `pauseEmbeddings()`    | Pause indexing loop                                         |
| `getIndexProgress()`   | JSON progress string `{processed,total,status,currentFile}` |
| `getDatabaseStats()`   | JSON stats by media type                                    |
| `searchImages(query)`  | JSON array of search results                                |

From `src/VantaEngineModule.ts` (used directly by People/Entity screens):

| Function                        | Purpose                           |
| ------------------------------- | --------------------------------- |
| `getTopEntities()`              | JSON array of face entities       |
| `getBestFaceCrop(entityId)`     | Best thumbnail crop for an entity |
| `setEntityName(entityId, name)` | Rename a cluster                  |
| `getEntityNeighbors(entityId)`  | People who appear in same photos  |
| `getEntityFiles(entityId)`      | Photos belonging to entity        |

## 5. Build / model setup

The app does **not** ship models in Git. The README describes the setup:

1. Download CLIP tokenizer files (`vocab.json`, `merges.txt`) from HuggingFace.
2. Run Python scripts in `produce_model/` to generate `clip_image_fp16.onnx`
   and `clip_text_fp16.onnx`.
3. Copy buffalo_sc models (`det_500m.onnx`, `w600k_mbf.onnx`) into app assets.
4. Place all six files in `android/app/src/main/assets/VantaModels/`.

At runtime the Kotlin module copies them from assets into
`context.filesDir/VantaModels/`. The C++ layer never hard-codes the package
path; it receives the directory from `VantaEngineConfig.kt` via
`setModelsDirNative`.

## 6. Notable code conventions

- TypeScript `strict: true` is enabled.
- ESLint + Prettier are configured and enforced in CI.
- Jest tests live in `__tests__/` and run with `jest-expo`.
- Screens use functional components + hooks.
- Theme colors are centralized in `ThemeContext.tsx`.
- Native code uses JNI directly; changes to signatures must be mirrored in
  Kotlin + C++ + TS.
- C++ uses C++20 in both CMake and Gradle `cppFlags`.

## 7. Known rough edges / watch-outs

- **iOS is scaffolded but largely unimplemented.** The `ios/` folder exists,
  and OpenCV ships an iOS framework, but there is no Obj-C/Swift bridge or
  iOS-specific model loading.
- **Media type filters removed.** The filter chips in `SearchScreen` were
  UI-only and have been removed. They can be re-added once the query engine
  supports filtering by MIME type.
- **NER/BERT excluded from build.** The source files under
  `Include/Query/Intent` and `Include/Query/NER_BERT` are kept in the tree but
  are not compiled or linked into search.

## 8. File map for quick navigation

| Concern                | File(s)                                                                                         |
| ---------------------- | ----------------------------------------------------------------------------------------------- |
| App entry + navigation | `App.tsx`, `index.ts`                                                                           |
| Theme                  | `ThemeContext.tsx`                                                                              |
| Error handling         | `ErrorBoundary.tsx`, `hooks/useVantaError.ts`                                                   |
| Search UI              | `screens/SearchScreen.tsx`                                                                      |
| Indexing UI            | `screens/SettingsScreen.tsx`                                                                    |
| People / faces UI      | `screens/PeopleScreen.tsx`, `screens/EntityDetailScreen.tsx`                                    |
| Debug DB UI            | `screens/DebugScreen.tsx`                                                                       |
| JS bridge exports      | `modules/vanta-bridge/index.ts`                                                                 |
| Native module types    | `modules/vanta-bridge/src/VantaEngine.types.ts`                                                 |
| Kotlin bridge          | `modules/vanta-bridge/android/src/main/java/expo/modules/vantaengine/VantaEngineModule.kt`      |
| Path config            | `modules/vanta-bridge/android/src/main/java/expo/modules/vantaengine/VantaEngineConfig.kt`      |
| Media scanner          | `modules/vanta-bridge/android/src/main/java/expo/modules/vantaengine/Android_scan.kt`           |
| Foreground service     | `modules/vanta-bridge/android/src/main/java/expo/modules/vantaengine/IndexingService.kt`        |
| JNI entry points       | `modules/vanta-bridge/cpp/vanta.cpp`                                                            |
| Runtime config         | `modules/vanta-bridge/cpp/Include/Config.hpp`                                                   |
| Safe JSON helpers      | `modules/vanta-bridge/cpp/Include/json_utils.hpp`                                               |
| C++ build              | `modules/vanta-bridge/cpp/CMakeLists.txt`                                                       |
| DB operations          | `modules/vanta-bridge/cpp/DB/DBoperations.cpp/.hpp`                                             |
| CLIP vector DB         | `modules/vanta-bridge/cpp/DB/clip_db.cpp/.hpp`                                                  |
| Face DB / clustering   | `modules/vanta-bridge/cpp/DB/face_DB.cpp/.hpp`, `modules/vanta-bridge/cpp/DB/graph_db.cpp/.hpp` |
| CLIP model             | `modules/vanta-bridge/cpp/Include/Preprocessing/CLIP/CLIP_model.cpp/.hpp`                       |
| CLIP tokenizer         | `modules/vanta-bridge/cpp/Include/Preprocessing/CLIP/CLIP_tokenizer.cpp/.hpp`                   |
| Face model             | `modules/vanta-bridge/cpp/Include/Preprocessing/Segregation/Seg_model.cpp/.hpp`                 |
| Query engine           | `modules/vanta-bridge/cpp/Include/Query/Query_processing/query_engine.cpp/.hpp`                 |

## 9. How to validate changes

- Format check: `npm run format:check`
- Lint: `npm run lint`
- Type check: `npm run typecheck`
- Tests: `npm test`
- Android native module build: `cd android && ./gradlew :vanta-bridge:assembleDebug`
- Full Android build: `npx expo run:android`
