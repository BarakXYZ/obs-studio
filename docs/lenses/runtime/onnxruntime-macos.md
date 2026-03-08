# Lenses ONNX Runtime on macOS

This document defines the reproducible path for building a CoreML-enabled ONNX Runtime for `plugins/lenses`.

## Why

The Lenses `ort-coreml` backend requires an ORT build that actually includes the CoreML execution provider.
If CoreML is not compiled into the dylib, runtime falls back to CPU and performance drops significantly.

## Build ORT (CoreML-enabled)

From repo root:

```bash
plugins/lenses/scripts/build-onnxruntime-macos.sh
```

Optional environment overrides:

```bash
ORT_VERSION=v1.19.2 \
ORT_ARCH=arm64 \
ORT_CONFIG=Release \
ORT_DEPLOY_TARGET=13.3 \
plugins/lenses/scripts/build-onnxruntime-macos.sh
```

Eigen download hash drift can break older ORT releases. The script now auto-detects a local Eigen install
and uses it when available.

```bash
# Optional controls:
ORT_USE_PREINSTALLED_EIGEN=auto   # auto (default) | on | off
ORT_EIGEN_PATH=/opt/homebrew/include/eigen3
ORT_USE_FULL_PROTOBUF=on          # on (default) | off
```

Supported `ORT_ARCH` values:
- `arm64` (default)
- `x86_64`
- `universal2` (builds both + lipo merge)

The script outputs a local bundle at:

```text
plugins/lenses/.deps_vendor/onnxruntime
```

## Wire into OBS build

```bash
cmake -S . -B build_macos -DLENSES_ONNXRUNTIME_ROOT="$PWD/plugins/lenses/.deps_vendor/onnxruntime"
cmake --build build_macos --target obs-studio -j12
```

`plugins/lenses/CMakeLists.txt` also auto-detects the bundled path when `LENSES_ONNXRUNTIME_ROOT` is not set.

## Verify runtime provider

Launch OBS and confirm logs show CoreML is enabled (not CPU fallback):

```text
[lenses] CoreML provider enabled ...
[lenses] runtime backend=ort ... detail='ORT session ready (provider=coreml)'
```
