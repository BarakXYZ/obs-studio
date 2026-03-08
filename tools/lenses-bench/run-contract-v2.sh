#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

BUILD_DIR="build_macos"
CONFIG="RelWithDebInfo"
MODEL_PATH="$ROOT_DIR/plugins/lenses/data/models/yolo11s-seg-coco80/model.onnx"
PROVIDER="ort-coreml"
INPUT_WIDTH="640"
INPUT_HEIGHT="640"
TARGET_FPS="30"
OUTPUT_DIR=""
INCLUDE_FAST="0"
SKIP_BUILD="0"
HISTORY_DB="$ROOT_DIR/bench/results/lenses-bench/history/history.sqlite"
RETENTION="100"
PRUNE_RUN_DIRS="0"
COMPARE_OLD_SELECTOR="previous"

# Contract v2 defaults (override if needed)
THRESHOLD_MIN_VALID_RATE="100"
THRESHOLD_MIN_COMPLETE_FPS="8"
THRESHOLD_MAX_INFER_P95_MS="110"
THRESHOLD_MAX_QUEUE_P95_MS="220"
THRESHOLD_MAX_E2E_P95_MS="320"

usage() {
  cat <<USAGE
Usage: tools/lenses-bench/run-contract-v2.sh [options]

Runs Benchmark Contract v2 with strict validity gates.
Default lane is full (30s+60s) because full-lane metrics are canonical.
Fast lane is optional diagnostic mode.

Options:
  --include-fast                Include fast lane (10s) with repeat=3
  --build-dir <dir>             Build directory (default: build_macos)
  --config <cfg>                Build config (default: RelWithDebInfo)
  --model <path>                ONNX model path
  --provider <name>             Provider (default: ort-coreml)
  --input-width <px>            Runtime input width (default: 640)
  --input-height <px>           Runtime input height (default: 640)
  --target-fps <fps>            Target submit fps mode target (default: 30)
  --output-dir <dir>            Output directory
  --history-db <path>           History DB path (default: bench/results/lenses-bench/history/history.sqlite)
  --retention <n>               Keep latest N runs in history DB (default: 100)
  --prune-run-dirs              Remove run directories evicted by retention policy
  --compare-old <selector>      Comparison baseline selector (default: previous)
                                (examples: previous, previous-any, days:3, <run-id>)
  --skip-build                  Skip build
  --threshold-min-valid-rate <pct>
  --threshold-min-complete-fps <v>
  --threshold-max-infer-p95-ms <v>
  --threshold-max-queue-p95-ms <v>
  --threshold-max-e2e-p95-ms <v>
  -h, --help                    Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --include-fast) INCLUDE_FAST="1"; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --model) MODEL_PATH="$2"; shift 2 ;;
    --provider) PROVIDER="$2"; shift 2 ;;
    --input-width) INPUT_WIDTH="$2"; shift 2 ;;
    --input-height) INPUT_HEIGHT="$2"; shift 2 ;;
    --target-fps) TARGET_FPS="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --history-db) HISTORY_DB="$2"; shift 2 ;;
    --retention) RETENTION="$2"; shift 2 ;;
    --prune-run-dirs) PRUNE_RUN_DIRS="1"; shift ;;
    --compare-old) COMPARE_OLD_SELECTOR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD="1"; shift ;;
    --threshold-min-valid-rate) THRESHOLD_MIN_VALID_RATE="$2"; shift 2 ;;
    --threshold-min-complete-fps) THRESHOLD_MIN_COMPLETE_FPS="$2"; shift 2 ;;
    --threshold-max-infer-p95-ms) THRESHOLD_MAX_INFER_P95_MS="$2"; shift 2 ;;
    --threshold-max-queue-p95-ms) THRESHOLD_MAX_QUEUE_P95_MS="$2"; shift 2 ;;
    --threshold-max-e2e-p95-ms) THRESHOLD_MAX_E2E_P95_MS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

lane="full"
if [[ "$INCLUDE_FAST" == "1" ]]; then
  lane="all"
fi
if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$ROOT_DIR/bench/results/lenses-bench/contract-v2-$(date +%Y%m%d-%H%M%S)"
fi

cmd=(
  "$ROOT_DIR/tools/lenses-bench/run-lane-bench.sh"
  --lane "$lane"
  --build-dir "$BUILD_DIR"
  --config "$CONFIG"
  --model "$MODEL_PATH"
  --provider "$PROVIDER"
  --input-width "$INPUT_WIDTH"
  --input-height "$INPUT_HEIGHT"
  --target-fps "$TARGET_FPS"
  --submit-fps-mode target
  --warmup-seconds 2
  --repeat-full 2
  --repeat-fast 3
  --expected-backend ort
  --expected-provider "$PROVIDER"
  --threshold-min-valid-rate "$THRESHOLD_MIN_VALID_RATE"
  --threshold-min-complete-fps "$THRESHOLD_MIN_COMPLETE_FPS"
  --threshold-max-infer-p95-ms "$THRESHOLD_MAX_INFER_P95_MS"
  --threshold-max-queue-p95-ms "$THRESHOLD_MAX_QUEUE_P95_MS"
  --threshold-max-e2e-p95-ms "$THRESHOLD_MAX_E2E_P95_MS"
  --fail-on-error
  --fail-on-invalid
  --fail-on-threshold
  --output-dir "$OUTPUT_DIR"
)

if [[ "$SKIP_BUILD" == "1" ]]; then
  cmd+=(--skip-build)
fi

set +e
"${cmd[@]}"
lane_exit=$?
set -e

summary_path="$OUTPUT_DIR/summary.md"
history_script="$ROOT_DIR/tools/lenses-bench/contract-history.sh"
run_id=""
if [[ -f "$summary_path" ]]; then
  register_cmd=(
    "$history_script" register
    --run-dir "$OUTPUT_DIR"
    --history-db "$HISTORY_DB"
    --retention "$RETENTION"
  )
  if [[ "$PRUNE_RUN_DIRS" == "1" ]]; then
    register_cmd+=(--prune-run-dirs)
  fi
  run_id="$("${register_cmd[@]}")"
  echo "[lenses-contract-v2] registered_run_id=$run_id"

  compare_out="$OUTPUT_DIR/compare-to-previous.md"
  set +e
  "$history_script" compare \
    --history-db "$HISTORY_DB" \
    --new "$run_id" \
    --old "$COMPARE_OLD_SELECTOR" \
    --output "$compare_out" >/dev/null
  compare_exit=$?
  set -e
  if [[ "$compare_exit" -eq 0 ]]; then
    echo "[lenses-contract-v2] comparison_report=$compare_out"
  else
    echo "[lenses-contract-v2] comparison skipped (no previous compatible run found yet)"
  fi
fi

exit "$lane_exit"
