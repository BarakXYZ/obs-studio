# Lenses Benchmark Contract v2

This directory defines the reproducible, validity-gated benchmark contract for production perf baselines.

## Policy

1. Full lane (`30s` + `60s`) is the canonical baseline lane.
2. Fast lane (`10s`) is diagnostic-only and should be used sparingly.
3. Baseline summaries must include validity gates, confidence stats, and threshold evaluation.
4. Production-valid runs require strict GPU determinism:
   - `cpu_fallback_detected=0`
   - `cpu_fallback_disabled=1`

## Contract Runner

Run canonical Contract v2 baseline (full lane by default):

```bash
tools/lenses-bench/run-contract-v2.sh
```

Include fast lane only when needed for targeted iteration:

```bash
tools/lenses-bench/run-contract-v2.sh --include-fast
```

Each Contract v2 run auto-registers into local history and writes:

1. `<run-dir>/summary.md`
2. `<run-dir>/summary.tsv`
3. `<run-dir>/compare-to-previous.md` (when a compatible previous run exists)

History DB default location:

`/Users/barakxyz/personal/desktop-avatar/obs-studio/bench/results/lenses-bench/history/history.sqlite`

List recent runs:

```bash
tools/lenses-bench/contract-history.sh list --limit 20
```

Compare any two runs (or auto-previous):

```bash
tools/lenses-bench/contract-history.sh compare --new latest --old previous
```

Compare against older historical points:

```bash
# Any immediately previous run by timestamp
tools/lenses-bench/contract-history.sh compare --new latest --old previous-any

# Strict profile-compatible run at least 3 days older
tools/lenses-bench/contract-history.sh compare --new latest --old days:3
```

Retention policy (default latest 100 runs kept in DB):

```bash
tools/lenses-bench/contract-history.sh register --run-dir <run-dir> --retention 100
```

## Lane Runner (Raw)

`run-lane-bench.sh` is the underlying lane harness. It uses real clip frames (`ffmpeg`) and feeds
them into `lenses-benchmark` with:

1. time-window-driven submission (`duration_s * submit_fps`)
2. uncaptured warmup window (`--warmup-seconds`)
3. validity gates (health/backend/provider)
4. strict CPU-fallback gates (`cpu_fallback_detected=0`, `cpu_fallback_disabled=1`)
5. steady-state percentile export (`p50/p95/p99`) for infer/queue/e2e

Example:

```bash
tools/lenses-bench/run-lane-bench.sh \
  --lane full \
  --provider ort-coreml \
  --submit-fps-mode target \
  --target-fps 30 \
  --warmup-seconds 2 \
  --repeat-full 2 \
  --expected-backend ort \
  --expected-provider ort-coreml
```

CoreML coverage gate example:

```bash
tools/lenses-bench/run-lane-bench.sh \
  --lane full \
  --provider ort-coreml \
  --expected-backend ort \
  --expected-provider ort-coreml \
  --threshold-min-coreml-coverage-pct 95 \
  --fail-on-threshold
```

Run a model-tier CoreML coverage audit (fallback allowed for measurement only):

```bash
tools/lenses-bench/run-coreml-coverage-audit.sh --lane fast --repeat 1
```

This writes a ranked table with `coreml_coverage_mean_pct`, throughput, and p95 infer
to quickly identify which bundled models are closest to strict GPU compatibility.

## Outputs

Each run exports:

1. `summary.md`: contract report with runs, valid-only aggregates, confidence stats, threshold gates
2. `summary.tsv`: machine-readable row-level metrics

## Legacy Harness

The runtime harness remains available for plugin-local smoke and telemetry parsing:

```bash
tools/lenses-bench/run-runtime-bench.sh --build-dir build_macos --config RelWithDebInfo
```
