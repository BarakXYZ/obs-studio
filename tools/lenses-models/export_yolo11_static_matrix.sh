#!/usr/bin/env bash
set -euo pipefail

# Export YOLO11 segmentation model tiers (n/s/m/l/x) as static-shape ONNX packages
# for Lenses and validate each package.
#
# Example:
#   tools/lenses-models/export_yolo11_static_matrix.sh \
#     --output-root plugins/lenses/data/models \
#     --ultralytics-repo /Users/barakxyz/personal/desktop-avatar/ultralytics \
#     --python python3

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT_ROOT="$ROOT_DIR/plugins/lenses/data/models"
ULTRALYTICS_REPO="$ROOT_DIR/../ultralytics"
PYTHON_BIN="python3"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-root)
      OUTPUT_ROOT="$2"; shift 2 ;;
    --ultralytics-repo)
      ULTRALYTICS_REPO="$2"; shift 2 ;;
    --python)
      PYTHON_BIN="$2"; shift 2 ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1 ;;
  esac
done

if [[ ! -d "$ULTRALYTICS_REPO" ]]; then
  echo "Ultralytics repo not found: $ULTRALYTICS_REPO" >&2
  exit 1
fi

mkdir -p "$OUTPUT_ROOT"
export PYTHONPATH="$ULTRALYTICS_REPO:${PYTHONPATH:-}"

declare -A IMGSZ=(
  [n]=512
  [s]=512
  [m]=640
  [l]=768
  [x]=768
)

for TIER in n s m l x; do
  MODEL_ID="yolo11${TIER}-seg-coco80"
  MODEL_FILE="yolo11${TIER}-seg.pt"
  PACKAGE_DIR="$OUTPUT_ROOT/$MODEL_ID"
  SIZE="${IMGSZ[$TIER]}"
  MODEL_NAME="YOLO11${TIER} Segmentation (COCO80)"

  echo "[lenses-models] Exporting $MODEL_ID (imgsz=$SIZE, static input)"
  "$PYTHON_BIN" "$ROOT_DIR/tools/lenses-models/export_ultralytics_onnx.py" \
    --model "$MODEL_FILE" \
    --output-dir "$PACKAGE_DIR" \
    --imgsz "$SIZE" \
    --batch 1 \
    --no-dynamic \
    --simplify \
    --no-half \
    --no-nms \
    --id "$MODEL_ID" \
    --name "$MODEL_NAME" \
    --size-tier "$TIER" \
    --bundled \
    --license-note "Review and comply with upstream model and dataset licenses before commercial distribution."

  "$PYTHON_BIN" "$ROOT_DIR/tools/lenses-models/validate_model_package.py" \
    --package-dir "$PACKAGE_DIR"
done

echo "[lenses-models] Export matrix complete: $OUTPUT_ROOT"
