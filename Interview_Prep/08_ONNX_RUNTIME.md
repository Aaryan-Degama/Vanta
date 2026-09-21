# 08 — ONNX Runtime & Numeric Precision

## What Vanta actually runs [VERIFIED-REPO]

| Property | Value | Evidence |
|---|---|---|
| Runtime | **ONNX Runtime 1.26.0** | version string in `libonnxruntime.so` |
| Binary | prebuilt shared lib, 27 MB, ELF aarch64, NDK r28, min Android 24 | `file(1)` |
| Execution provider | **CPU only** | No `SessionOptions::AppendExecutionProvider*` call exists anywhere in the repo. `nnapi_provider_factory.h` is present in the headers but never `#include`d or called |
| `Ort::Env` instances | **3** — one per model class | `CLIP_model.hpp::env`, `Seg_model.hpp::shared_env`, `ner.hpp::env_` |
| Thread config | CLIP: **default**. Face: **default**. NER: `SetIntraOpNumThreads(1)`, `SetInterOpNumThreads(1)` | source |
| Graph optimisation | CLIP/face: default (`ORT_ENABLE_ALL` is the default). NER: explicit `ORT_ENABLE_ALL` | source |
| Session lifetime | Created lazily on first use, stored in file-scope globals, **never destroyed** | `vanta.cpp` |

---

## ONNX and ONNX Runtime — the concepts

**ONNX** is an interchange format: a protobuf-serialised **directed acyclic computation graph**. Nodes are operators
from a versioned standard set (an *opset*), edges are typed tensors, and weights are stored as `initializer`s. It
decouples the training framework from the inference engine — you train in PyTorch and run anywhere that implements
the opset.

**ONNX Runtime** is an inference engine over that graph. Roughly:

```mermaid
flowchart LR
    A[".onnx protobuf"] --> B["Graph load & shape inference"]
    B --> C["Graph optimisations<br/>constant folding, op fusion,<br/>redundant-node elimination,<br/>layout transforms"]
    C --> D["Partitioning across<br/>execution providers"]
    D --> E["Kernel selection per node"]
    E --> F["Memory planning<br/>(arena, buffer reuse)"]
    F --> G["Run(): execute plan"]
```

Key ideas to be able to explain:
- **Opset version** pins operator semantics. Vanta's models use opset **18** (CLIP, NER) and **11** (buffalo_l, as
  shipped by InsightFace). ORT 1.26 supports both.
- **Graph optimisations** happen at session creation, which is why loading a model is slow and inference is fast.
  Basic = constant folding / redundant node elimination; Extended = fusions (Conv+BN+ReLU, attention fusion);
  Layout = NCHW↔NHWC for the target. `ORT_ENABLE_ALL` enables all tiers.
- **Execution providers (EPs)** are pluggable backends. ORT partitions the graph and assigns each subgraph to the
  highest-priority EP that supports it, falling back to CPU. Mobile-relevant EPs: **CPU** (default, uses MLAS with
  ARM NEON kernels), **NNAPI**, **XNNPACK**, **QNN** (Qualcomm Hexagon NPU), **CoreML** (iOS).
- **Session vs Run.** The `Ort::Session` holds weights and the execution plan and is expensive to build. `Run()` is
  cheap and **thread-safe for concurrent calls**. The `Ort::Env` owns the logging sink and the shared thread pools.

---

## Why ONNX Runtime for Vanta?

| Alternative | Why not chosen |
|---|---|
| **TensorFlow Lite** | Excellent on Android, smallest runtime, best NNAPI/GPU delegate story. **But:** CLIP and the InsightFace models are distributed as PyTorch/ONNX. Getting to TFLite means PyTorch→ONNX→TF→TFLite, a conversion chain that is fragile for transformers and where InsightFace's SCRFD/ArcFace are not officially published. You'd own the conversion risk for all three models |
| **ExecuTorch** | PyTorch's own on-device runtime; strategically the right long-term bet and a better PyTorch story. In mid-2026 it is newer, has a smaller op-coverage surface for arbitrary third-party models, and there is no official ExecuTorch InsightFace |
| **Core ML** | iOS only |
| **MediaPipe** | Great for its own bundled solutions (face detection, landmarks), but it is a pipeline framework, not a general model runtime. Would not run CLIP or the InsightFace ArcFace head |
| **NCNN / MNN** | Genuinely fast, tiny, ARM-optimised. **Cost:** a separate converter per model, weaker transformer/op coverage historically, smaller ecosystem, and more per-model debugging |
| **TensorRT** | NVIDIA-only. Irrelevant on Android |

**The decisive argument:** *all three* of Vanta's models are natively available as ONNX.
InsightFace **ships buffalo_l as `.onnx` directly** — no conversion at all. CLIP exports to ONNX in ~20 lines of
`torch.onnx.export` (see `produce_model/get_onnx_CLIP.py`). The NER model comes out of HuggingFace via the same path.
**One runtime, three models, zero custom converters, and a C++ API.** For a solo project on a deadline, that
consolidation is the whole argument — and it's a much better answer than "ONNX Runtime is fast."

---

## Session configuration — what's there, what's missing

```cpp
// CLIP_model.cpp — session_options is DEFAULT-CONSTRUCTED. No configuration at all.
Ort::SessionOptions session_options;
vision_session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
text_session   = std::make_unique<Ort::Session>(env, text_model_path.c_str(), session_options);
```

```cpp
// ner.cpp — the only model that is configured
Ort::SessionOptions opts;
opts.SetIntraOpNumThreads(1);
opts.SetInterOpNumThreads(1);
opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
```

**Why 1 thread for NER is correct:** BERT-tiny at sequence length 32 is a tiny workload (~10 M FLOPs). Thread
dispatch and synchronisation overhead would exceed the compute. The comment also notes NNAPI is avoided because
BERT ops aren't reliably supported across NNAPI implementations — which is accurate and a good instinct.

**What's missing, in priority order:**

| Gap | Impact | Fix |
|---|---|---|
| **No intra-op thread limit on CLIP/face** | ORT defaults intra-op threads to the number of cores. During indexing that means ORT will happily saturate every big core, which is fast but is exactly what causes thermal throttling and battery drain on a multi-minute job | `SetIntraOpNumThreads(4)` or expose it as a user "performance vs battery" setting |
| **No XNNPACK EP** | XNNPACK is ORT's optimised CPU kernel library for ARM and is usually a solid win on quantised convnets — i.e. exactly SCRFD and ArcFace | `AppendExecutionProvider("XNNPACK")` — requires an ORT build with it enabled |
| **No NNAPI EP** | Could offload SCRFD/ArcFace to a DSP/NPU. NNAPI is deprecated as of Android 15 in favour of vendor runtimes, and its per-device behaviour is notoriously inconsistent, so *not* using it is defensible — but it should be a measured decision, not an unmade one | — |
| **Three `Ort::Env` objects** | ORT documents one `Env` per process. Each carries its own logging sink; with the default (non-shared) allocator config each session also gets its own arena, so peak memory is the sum of three high-water marks rather than one | One `static Ort::Env`, plus `EnableSharedAllocator` / `CreateAndRegisterAllocator` if you want a shared arena |
| **Sessions never released** | CLIP-image + SCRFD + ArcFace stay resident after indexing completes. `unload()` exists on both classes and is never called | Call `unload()` when the `IndexingService` stops |
| **Input/output names re-queried per call** | `Seg_model.cpp::get_embedding` calls `GetInputNameAllocated`/`GetOutputNameAllocated` **inside the per-face loop**; `CLIP_model.cpp` does it per inference. Each returns an allocator-backed string | Cache them at `load()` time. Small, but it's in the hottest loop |
| **Batch size is always 1** | `input_dims = {1,3,224,224}`. The exported CLIP graph has `dynamic_axes={"IMAGE":{0:"batch_size"}}`, so it **already supports batching** and the engine doesn't use it | See [16](./16_INDEXING_PIPELINE.md) — this is the single biggest indexing throughput win available |

---

## FP32 / FP16 / INT8 — and what Vanta actually uses

### The theory

| Format | Bits | Range / precision | Effect |
|---|---|---|---|
| **FP32** | 32 | ~7 decimal digits, exp ±38 | Baseline. 4 bytes/param |
| **FP16** | 16 | ~3 decimal digits, exp ±65504 | Half the storage and half the memory bandwidth. **Narrow range — overflow/underflow is the real risk**, especially in attention-softmax denominators and accumulations |
| **BF16** | 16 | FP32's exponent range, 8-bit mantissa | Much safer than FP16 for training; poorly supported on mobile CPUs |
| **INT8** | 8 | 256 levels + a scale (and zero-point) | Quarter storage. **Requires calibration** (static) or per-tensor runtime ranges (dynamic). Integer MACs can use dedicated ARM instructions (`SDOT`/`UDOT` on ARMv8.2+) |

**Quantisation formula:** `real ≈ scale × (q − zero_point)`, where for asymmetric uint8
`scale = (max−min)/255` and `zero_point = round(−min/scale)`.

- **Dynamic quantisation** stores weights as int8/uint8 and computes activation scales *at runtime*, per inference.
  No calibration data needed. Cost: a `DynamicQuantizeLinear` node per activation.
- **Static quantisation** calibrates activation ranges offline on a representative dataset. Faster at runtime,
  better accuracy, but requires data — which for a privacy project you don't have.

### **Why FP16 does NOT automatically mean 2× faster**

This is the question. The answer has four parts:

1. **It halves *bandwidth*, not necessarily *arithmetic*.** If a kernel is memory-bound (loading weights dominates),
   halving bytes ≈ halves time. If it's compute-bound, it doesn't.
2. **The CPU may not have native FP16 arithmetic.** ARMv8.0 has FP16 *storage* (`__fp16`) but ARMv8.2-A FP16
   extension is required for full-rate FP16 *vector arithmetic*. On a CPU without it, ORT converts FP16→FP32, does
   the math, converts back — which can be **slower than FP32**.
3. **Conversion costs are real.** Vanta pays them explicitly:
   ```cpp
   // CLIP_model.cpp::get_embedding — 150,528 scalar FP32→FP16 conversions per image
   for (size_t i = 0; i < fp16_data.size(); ++i) fp16_data[i] = Ort::Float16_t(src[i]);
   ```
   and again on output (512 FP16→FP32). On the input side that's 1×3×224×224 = **150,528** conversions per photo, in
   a scalar loop, on the critical path.
4. **Not every node runs in FP16.** `convert_float_to_float16` inserts `Cast` nodes around anything that must stay
   FP32; the graph becomes mixed precision with cast overhead at the boundaries.

> **Honest interview line:** *"I converted CLIP to FP16 primarily for **size** — it halves ~600 MB of weights to
> ~300 MB, which matters a lot for an app that ships models in its APK. I did not benchmark it against FP32 for
> latency, and I'd expect the speedup to be well under 2× and possibly negative on a CPU without the ARMv8.2 FP16
> extension. The FP32→FP16 input conversion is a scalar loop over 150k elements per image that I'd hoist into the
> preprocessing pass or eliminate by exporting an FP32-input/FP16-weight model."*

### What Vanta actually ships

| Model | Precision | Evidence | Rationale |
|---|---|---|---|
| **CLIP image** `clip_image_fp16.onnx` | **FP16** | `quantize.py` uses `onnxconverter_common.float16.convert_float_to_float16` | Size. ViT-B/32 visual ≈ 88 M params: **[ESTIMATED]** 352 MB FP32 → **176 MB FP16** |
| **CLIP text** `clip_text_fp16.onnx` | **FP16** | same | ≈ 63 M params: **[ESTIMATED]** 254 MB → **127 MB** |
| **SCRFD** `det_10g.onnx` | **INT8 dynamic (QUInt8)** | 48× `DynamicQuantizeLinear`, 58× `ConvInteger`; file is **4.11 MB** for 4,226,183 params ≈ 1 byte/param | `download_models.py`: `quantize_dynamic(..., weight_type=QUInt8)` |
| **ArcFace** `w600k_r50.onnx` | **INT8 dynamic (QUInt8)** | 53× `ConvInteger`, 1× `MatMulInteger`; **41.76 MB** for 43,591,296 params | same |
| **NER** `ner_model.onnx` | **FP32** | no quantisation ops in graph | It's only 16.94 MB; quantising 4.4 M params saves ~12 MB and risks accuracy on a model that's already tiny. Correct call |

**Why the split is actually defensible** (and a great answer to "why two different precisions?"):

> "The face models are convnets, which quantise to INT8 very well — that's the classic, well-validated case, and
> `quantize_dynamic` needs no calibration data, which I don't have because the whole point is that user photos never
> leave the device. CLIP is a Vision Transformer; INT8 dynamic quantisation of transformers is much more delicate —
> the attention softmax and LayerNorm are sensitive, and naive dynamic quantisation of a ViT typically costs real
> retrieval accuracy. FP16 is the conservative middle: it halves the size, which was my actual constraint, with
> essentially no accuracy change. And the NER model is 17 MB — there's nothing to win by quantising it."

**The honest caveat to volunteer:** *"I didn't measure the accuracy cost of INT8 on ArcFace. Published results for
INT8 ArcFace put the degradation at a small number of tenths of a percent on LFW-style benchmarks, but I have no
device-local measurement, and INT8 face embeddings are exactly the thing that would silently degrade clustering
quality — which is the weakest part of my system already. Measuring 1:1 verification TAR@FAR before and after
quantisation on a public set like LFW or IJB-C would be the first evaluation I'd add."*

---

## The `Ort::Value` memory contract — a likely code-level question

```cpp
std::vector<float> input_tensor_values(1*3*640*640);          // engine owns this
Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
    memory_info_d, input_tensor_values.data(), input_tensor_values.size(),
    input_dims.data(), input_dims.size());                     // NON-OWNING VIEW
auto output_tensors = detector_session->Run(...);              // vector must still be alive here
```

`CreateTensor<T>(MemoryInfo, T* p, size_t count, ...)` does **not** copy — it wraps the caller's buffer. If
`input_tensor_values` went out of scope or reallocated before `Run()`, you'd read freed memory. Vanta gets this right
everywhere (the vectors are function-local and outlive the `Run`), but it's the classic ORT C++ footgun and worth
being ready to explain.

Conversely, `Run()` returns `std::vector<Ort::Value>` where ORT owns the output buffers; `GetTensorData<T>()` gives a
pointer valid only while those `Ort::Value`s live. Vanta copies out immediately (`std::vector<float>
embedding(raw_output, raw_output + 512)`), which is correct.

`OrtArenaAllocator` (used in all three `MemoryInfo`s) means ORT sub-allocates from large pre-reserved blocks rather
than calling `malloc` per tensor — it trades peak RSS for allocation speed, and it's why ORT memory doesn't return to
the OS between inferences.
