# Lenses Plugin

`lenses` is an OBS filter plugin intended to host scalable, production-grade realtime enhancement passes.

## Architecture

- `lenses-module.c`: module entrypoint and source registration.
- `include/lenses/lenses-filter.h`: public filter source declaration.
- `include/lenses/core/`: mask-first engine contracts, runtime interfaces, and C bridge entrypoints.
- `src/filter/lenses-filter.c`: filter lifecycle, settings, color-space aware capture, multi-pass orchestration.
- Filter properties include a debug inspector surface (runtime counters, latest detections, and rule stack/hit trace) plus class-mask overlay controls.
- Model UX is dropdown-only: explicit model preset + quality tier selector (`auto`, `n`, `s`, `m`, `l`, `x`) routed through the in-app catalog.
- `include/lenses/io/` + `src/io/`: policy preset schema compile/validate helpers and legacy invert migration adapters.
- `include/lenses/io/lenses-model-catalog.h` + `src/io/lenses-model-catalog.c`: model package discovery across bundled and user model roots.
- `src/core/`: no-op mask runtime, deterministic rule compiler skeleton, registry, and compositor contract implementations.
- `src/core/noop-mask-generator.*`: bounded async submit/output queues with drop-oldest policy, telemetry counters, and last-mask fallback behavior.
- `include/lenses/ai/runtime/` + `src/ai/`: model registry contracts and backend factory wiring (noop + ORT adapter path).
- `include/lenses/ai/decode/` + `src/ai/decode/`: segmentation tensor decode utilities for per-instance masks and per-class union masks.
- `include/lenses/ai/tracking/` + `src/ai/tracking/`: class-aware ByteTrack-style ID association for persistent track IDs.
- `include/lenses/ai/cloud/` + `src/ai/cloud/`: feature-gated cloud backend scaffold with local fallback under the same mask contract.
- `src/pipeline/lenses-pass.c/.h`: reusable GPU effect pass loader/renderer abstraction.
- `data/effects/grayscale.effect`, `data/effects/solid-color.effect`, `data/effects/masked-blend.effect`: baseline multi-chain graph nodes and deterministic region compositor shader.
- `data/effects/`: pass shaders (`.effect`).
- `data/models/`: bundled AI model packages (`metadata.json`, `model.onnx`, `class-map.json`).
- `data/locale/`: translatable UI strings.
- `test/segmentation-decoder-test.cpp`: decoder correctness smoke tests.
- `test/bytetrack-tracker-test.cpp`: tracker ID stability and occlusion retention tests.

## Pipeline Model

The filter captures the target source into a color-space aware render target, executes enabled passes in a ping-pong chain, then presents the final texture with explicit color-space conversion.

This keeps pass composition deterministic and allows adding future passes without rewriting filter plumbing.

## Current Passes

- `invert`: white-region-targeted invert pass for early "Dark Mode Everything" behavior.
  - Includes local-region floor, thin-text suppression, edge suppression, and temporal smoothing controls.

## Adding a Pass

1. Add a new effect in `data/effects/`.
2. Add a `lenses_pass` entry in `lenses_initialize_passes`.
3. Implement pass `load_params` and `apply_params` callbacks for cached uniforms.
4. Add settings + locale strings if user controls are needed.

## Test and Benchmark

Build test targets:

```bash
cmake --build build_macos --config RelWithDebInfo \
  --target lenses-segmentation-decode-test \
  --target lenses-bytetrack-tracker-test \
  --target lenses-runtime-soak-test \
  --target lenses-benchmark
```

Run:

```bash
./build_macos/plugins/lenses/RelWithDebInfo/lenses-segmentation-decode-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-bytetrack-tracker-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-runtime-soak-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-benchmark
```
