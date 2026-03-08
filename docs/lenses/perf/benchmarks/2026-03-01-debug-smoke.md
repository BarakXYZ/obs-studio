# Lenses Benchmark Baseline Record

- Date: 2026-03-01
- Machine: Apple M4 Max (local dev workstation)
- Build: `build_macos` / `Debug`
- OBS version: `0.0.1` (local fork)
- Branch + commit: `codex/lenses` @ `ad9dbdb2f`

## Runtime Config

- Backend: test binaries (`lenses-*` smoke run)
- Model preset: n/a (synthetic benchmark binary)
- Model quality: n/a
- Input size: synthetic benchmark defaults
- Target FPS: n/a
- Inference every N: n/a
- Similarity skip: n/a
- I/O binding: n/a

## Results

- submit_fps_mean: n/a
- complete_fps_mean: n/a
- drop_fps_mean: n/a
- complete_fps_min/max: n/a
- submit_queue_depth (steady-state): `8` (runtime soak test output)
- output_queue_depth (steady-state): `4` (runtime soak test output)

## Stage Metrics

- readback_ms_last: n/a
- preprocess_ms_last: n/a
- infer_ms_last: n/a
- decode_ms_last: `2.51236` (benchmark mean decode ms)
- track_ms_last: `0.0163604` (benchmark mean tracker ms)
- readback_ms_p95: n/a
- preprocess_ms_p95: n/a
- infer_ms_p95: n/a
- decode_ms_p95: not emitted by benchmark (p99 available: `2.99308`)
- track_ms_p95: not emitted by benchmark (p99 available: `0.026459`)

## Health

- runtime ready: pass (all binaries completed successfully)
- fallback active: n/a
- provider health detail: n/a
- coreml coverage: n/a

## Notes

- Command used: `tools/lenses-bench/run-runtime-bench.sh --build-dir build_macos --config Debug`
- Binary results:
  - `segmentation-decoder-test: PASS`
  - `bytetrack-tracker-test: PASS`
  - `lenses-runtime-soak-test: PASS`
  - `lenses-benchmark` decode/tracker metrics recorded above.
- Next action: capture real OBS telemetry with `--obs-log` for `s@512`, `m@512`, and `s@960x540` matrix runs.
