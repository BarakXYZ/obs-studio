# Lenses Benchmarks and Soak

## Build

```bash
cmake -S . -B build_macos -G Xcode -DBUILD_TESTS=ON
cmake --build build_macos --config RelWithDebInfo \
  --target lenses-benchmark lenses-runtime-soak-test \
  --target lenses-segmentation-decode-test lenses-bytetrack-tracker-test
```

## Run

```bash
./build_macos/plugins/lenses/RelWithDebInfo/lenses-segmentation-decode-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-bytetrack-tracker-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-runtime-soak-test
./build_macos/plugins/lenses/RelWithDebInfo/lenses-benchmark
```

## Output Capture

Record:

- decode mean/p50/p99 ms
- tracker mean/p50/p99 ms
- soak submitted/completed/dropped counters
- queue depth maxima

Keep a dated log per checkpoint commit when performance-sensitive changes land.

## Latest Local Baseline

Date: 2026-02-28 (M4 Max dev machine)

- `lenses-segmentation-decode-test`: PASS
- `lenses-bytetrack-tracker-test`: PASS
- `lenses-runtime-soak-test`: PASS
  - submitted=1500 completed=374 dropped=1441 reused=0 submit_q=8 output_q=4
- `lenses-benchmark`:
  - decode_ms mean=0.220632 p50=0.203334 p99=0.552
  - tracker_ms mean=0.0037575 p50=0.003375 p99=0.011083
