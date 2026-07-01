# Vanta

Vanta is a React Native (Expo) application that performs on-device, AI-powered
search over the photos and media on your phone. It uses a custom C++ native
module (`vanta-bridge`) with ONNX Runtime, OpenCV, and sqlite-vec to generate
CLIP embeddings and run vector search without sending data to a server.

> **Platform support:** Android is the primary, actively developed platform. iOS
> scaffolding exists under `ios/` but is **not wired to the C++ engine yet**.
> Building and running on iOS will require adding the Objective-C/Swift JNI
> equivalents and an iOS-specific CMake toolchain.

## Features

- On-device CLIP image + text embedding
- Face detection / recognition pipeline (buffalo_sc via ONNX Runtime)
- Vector search backed by sqlite-vec
- Dark/light theme support
- React error boundary + per-screen native error handling

## Tech stack

| Layer            | Technology                          |
| ---------------- | ----------------------------------- |
| Mobile framework | Expo 56 / React Native 0.85         |
| Language         | TypeScript, Kotlin, C++20           |
| ML inference     | ONNX Runtime                        |
| Computer vision  | OpenCV 4.x                          |
| Vector search    | sqlite-vec                          |
| Testing          | Jest, @testing-library/react-native |
| CI               | GitHub Actions                      |

## Quick start

### Prerequisites

- Node.js >= 22
- Android Studio + Android SDK + NDK 27.0.12077973
- JDK 17

### Install

```bash
npm install
```

### Prepare model assets

The engine expects the following files in
`android/app/src/main/assets/VantaModels/` at build time:

```bash
# CLIP tokenizer vocabulary
mkdir -p android/app/src/main/assets/VantaModels
curl -L -o android/app/src/main/assets/VantaModels/vocab.json \
  https://huggingface.co/openai/clip-vit-base-patch32/resolve/main/vocab.json
curl -L -o android/app/src/main/assets/VantaModels/merges.txt \
  https://huggingface.co/openai/clip-vit-base-patch32/resolve/main/merges.txt
```

For the quantized CLIP ONNX model, see `produce_model/README.md`.

Face-detection models (`det_500m.onnx`, `w600k_mbf.onnx`) live in
`modules/vanta-bridge/cpp/Include/Preprocessing/Segregation/.model/buffalo_sc`
and are copied to the same assets folder during a release build.

### Run on Android

```bash
npm run android
```

### Validate locally

```bash
# Formatting, linting, type checking, tests
npm run format:check
npm run lint
npm run typecheck
npm test

# Native module build
./gradlew :vanta-bridge:assembleDebug
```

## Project structure

```text
.
├── App.tsx                    # Root app component + error boundary
├── ThemeContext.tsx           # Dark/light theme provider
├── ErrorBoundary.tsx          # Global JS error boundary
├── hooks/                     # Shared hooks (e.g. useVantaError)
├── utils/                     # Pure helpers (formatting, etc.)
├── screens/                   # Application screens
├── modules/vanta-bridge/      # Expo native module
│   ├── android/               # Kotlin module code
│   ├── cpp/                   # C++ engine (ONNX, OpenCV, sqlite-vec)
│   └── src/                   # TypeScript module facade
├── assets/Vanta_models/       # Bundled model assets
├── __tests__/                 # Jest tests
└── .github/workflows/         # CI
```

## Native module architecture

- `VantaEngineModule.kt` exposes functions to JavaScript through Expo modules.
- `VantaEngineConfig.kt` centralizes paths (models directory, DB path) so the
  C++ layer never hard-codes package-specific paths.
- `Config.hpp` is a thread-safe C++ singleton that receives the runtime models
  directory from Kotlin. Model loaders resolve `clip_text.onnx`,
  `clip_image.onnx`, and tokenizer files through it.
- `json_utils.hpp` builds native JSON responses with proper string escaping.
- A global mutex protects concurrent creation of the CLIP session, face
  analyzer, and tokenizer from multiple JS calls.

## iOS status

An iOS project scaffold is present under `ios/`, but the C++ engine is only
built for Android today. To support iOS you would need to:

1. Add an iOS-specific `VantaEngineModule` Swift/Objective-C++ implementation.
2. Provide an iOS CMake toolchain or an Xcode-based native module setup.
3. Ship the ONNX Runtime and OpenCV iOS binaries.
4. Wire the same `setModelsDirNative` and search/indexing entry points.

## Roadmap / known future work

- **Media type filters:** the filter chips on the Search screen were removed
  because they were UI-only. A future version can pass selected MIME-type
  filters into the native query engine.
- **NER / intent parsing:** code under `Include/Query/Intent` and
  `Include/Query/NER_BERT` is kept in source control but excluded from the
  CMake build. When an on-device BERT NER model is ready, wire it back into
  `query_engine.cpp` after `get_corrected_query`.

## Contributing

Please keep the project passing CI before opening a PR:

```bash
npm run format:check && npm run lint && npm run typecheck && npm test
```

## License

See `LICENSE`.
