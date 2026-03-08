#!/usr/bin/env bash
set -euo pipefail

# Reproducible Lenses runtime benchmark harness (P0 baseline).
# Usage:
#   tools/lenses-bench/run-runtime-bench.sh --build-dir build_macos --config RelWithDebInfo [--obs-log /path/to/log.txt]
#   tools/lenses-bench/run-runtime-bench.sh ... --obs-log log.txt --min-complete-fps 12 --max-drop-fps 1
#   tools/lenses-bench/run-runtime-bench.sh ... --obs-log log.txt --min-infer-ceiling-fps 10

BUILD_DIR="build_macos"
CONFIG="RelWithDebInfo"
OBS_LOG=""
MIN_SUBMIT_FPS=""
MIN_COMPLETE_FPS=""
MAX_DROP_FPS=""
MIN_INFER_CEILING_FPS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"; shift 2 ;;
    --config)
      CONFIG="$2"; shift 2 ;;
    --obs-log)
      OBS_LOG="$2"; shift 2 ;;
    --min-submit-fps)
      MIN_SUBMIT_FPS="$2"; shift 2 ;;
    --min-complete-fps)
      MIN_COMPLETE_FPS="$2"; shift 2 ;;
    --max-drop-fps)
      MAX_DROP_FPS="$2"; shift 2 ;;
    --min-infer-ceiling-fps)
      MIN_INFER_CEILING_FPS="$2"; shift 2 ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1 ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="$ROOT_DIR/$BUILD_DIR/plugins/lenses/$CONFIG"

if [[ ! -d "$BIN_DIR" ]]; then
  echo "Missing bin dir: $BIN_DIR" >&2
  exit 1
fi

echo "[lenses-bench] root=$ROOT_DIR"
echo "[lenses-bench] bin=$BIN_DIR"

echo "[lenses-bench] Running unit/integration binaries"
"$BIN_DIR/lenses-segmentation-decode-test"
"$BIN_DIR/lenses-bytetrack-tracker-test"
"$BIN_DIR/lenses-runtime-soak-test"

echo "[lenses-bench] Running synthetic benchmark"
"$BIN_DIR/lenses-benchmark"

if [[ -n "$OBS_LOG" ]]; then
  if [[ ! -f "$OBS_LOG" ]]; then
    echo "OBS log does not exist: $OBS_LOG" >&2
    exit 1
  fi
  echo "[lenses-bench] Parsing OBS runtime telemetry from $OBS_LOG"
  SUMMARY="$("$ROOT_DIR/tools/lenses-bench/summarize-obs-telemetry.sh" "$OBS_LOG")"
  echo "$SUMMARY"

  submit_fps_mean="$(echo "$SUMMARY" | awk -F'[ =]' '/submit_fps_mean=/{print $2; exit}')"
  complete_fps_mean="$(echo "$SUMMARY" | awk -F'[ =]' '/complete_fps_mean=/{print $2; exit}')"
  drop_fps_mean="$(echo "$SUMMARY" | awk -F'[ =]' '/drop_fps_mean=/{print $2; exit}')"
  infer_ceiling_fps="$(echo "$SUMMARY" | awk -F'[ =]' '/infer_fps_ceiling_est=/{for(i=1;i<=NF;i++) if($i ~ /^infer_fps_ceiling_est=/){split($i,a,"="); print a[2]; exit}}')"

  if [[ -n "$MIN_SUBMIT_FPS" ]]; then
    awk -v actual="$submit_fps_mean" -v expected="$MIN_SUBMIT_FPS" \
      'BEGIN { if (actual+0 < expected+0) { exit 1 } }' || {
      echo "[lenses-bench] FAIL: submit_fps_mean=$submit_fps_mean < min_submit_fps=$MIN_SUBMIT_FPS" >&2
      exit 1
    }
  fi
  if [[ -n "$MIN_COMPLETE_FPS" ]]; then
    awk -v actual="$complete_fps_mean" -v expected="$MIN_COMPLETE_FPS" \
      'BEGIN { if (actual+0 < expected+0) { exit 1 } }' || {
      echo "[lenses-bench] FAIL: complete_fps_mean=$complete_fps_mean < min_complete_fps=$MIN_COMPLETE_FPS" >&2
      exit 1
    }
  fi
  if [[ -n "$MAX_DROP_FPS" ]]; then
    awk -v actual="$drop_fps_mean" -v expected="$MAX_DROP_FPS" \
      'BEGIN { if (actual+0 > expected+0) { exit 1 } }' || {
      echo "[lenses-bench] FAIL: drop_fps_mean=$drop_fps_mean > max_drop_fps=$MAX_DROP_FPS" >&2
      exit 1
    }
  fi
  if [[ -n "$MIN_INFER_CEILING_FPS" ]]; then
    if [[ -z "$infer_ceiling_fps" ]]; then
      echo "[lenses-bench] FAIL: infer_fps_ceiling_est missing from summary" >&2
      exit 1
    fi
    awk -v actual="$infer_ceiling_fps" -v expected="$MIN_INFER_CEILING_FPS" \
      'BEGIN { if (actual+0 < expected+0) { exit 1 } }' || {
      echo "[lenses-bench] FAIL: infer_fps_ceiling_est=$infer_ceiling_fps < min_infer_ceiling_fps=$MIN_INFER_CEILING_FPS" >&2
      exit 1
    }
  fi
fi
