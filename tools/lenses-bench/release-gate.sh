#!/usr/bin/env bash
set -euo pipefail

# Release gate wrapper for Lenses runtime KPIs.
# Example:
#   tools/lenses-bench/release-gate.sh \
#     --build-dir build_macos \
#     --config Debug \
#     --obs-log /path/to/obs.log \
#     --min-complete-fps 12 \
#     --max-drop-fps 1

BUILD_DIR="build_macos"
CONFIG="RelWithDebInfo"
OBS_LOG=""
MIN_SUBMIT_FPS="10"
MIN_COMPLETE_FPS="12"
MAX_DROP_FPS="1"

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
    *)
      echo "Unknown argument: $1" >&2
      exit 1 ;;
  esac
done

if [[ -z "$OBS_LOG" ]]; then
  echo "--obs-log is required for release gate checks" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

"$ROOT_DIR/tools/lenses-bench/run-runtime-bench.sh" \
  --build-dir "$BUILD_DIR" \
  --config "$CONFIG" \
  --obs-log "$OBS_LOG" \
  --min-submit-fps "$MIN_SUBMIT_FPS" \
  --min-complete-fps "$MIN_COMPLETE_FPS" \
  --max-drop-fps "$MAX_DROP_FPS"

echo "[lenses-bench] release gate passed"
