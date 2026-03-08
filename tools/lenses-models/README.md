# Lenses Model Tooling

This directory provides reproducible tooling for exporting and packaging ONNX segmentation models for `plugins/lenses`.

## Scripts

- `export_ultralytics_onnx.py`
  - Exports a YOLO model to ONNX using Ultralytics export APIs.
  - Builds a package directory with:
    - `model.onnx`
    - `metadata.json`
    - `class-map.json`
- `validate_model_package.py`
  - Validates package schema and integrity.
  - Optionally validates ONNX graph structure when `onnx` Python package is installed.
- `generate_coco_class_map.py`
  - Emits canonical COCO-80 class map JSON.
- `export_yolo11_static_matrix.sh`
  - Exports `yolo11{n,s,m,l,x}-seg` packages in one pass with static input shape defaults.
  - Validates each package after export.

## Quick Start

1. Export a segmentation model and package it:

```bash
python3 tools/lenses-models/export_ultralytics_onnx.py \
  --model yolo11n-seg.pt \
  --output-dir /tmp/lenses-models/yolo11n-seg \
  --imgsz 640 \
  --no-dynamic
```

2. Validate the generated package:

```bash
python3 tools/lenses-models/validate_model_package.py \
  --package-dir /tmp/lenses-models/yolo11n-seg
```

3. Regenerate canonical COCO class map file:

```bash
python3 tools/lenses-models/generate_coco_class_map.py \
  --output tools/lenses-models/class-maps/coco80.json
```

4. Export the full bundled tier matrix (n/s/m/l/x) with static input shapes:

```bash
tools/lenses-models/export_yolo11_static_matrix.sh \
  --output-root plugins/lenses/data/models \
  --ultralytics-repo /Users/barakxyz/personal/desktop-avatar/ultralytics \
  --python python3
```

## Packaging Contract (v1)

Required package files:

- `model.onnx`
- `metadata.json`
- `class-map.json`

`metadata.json` must include at least:

- `version`
- `lenses_schema`
- `model` (with `file`, optional `sha256`)
- `class_map_file`
- `class_count`

## References

- Ultralytics export mode: <https://docs.ultralytics.com/modes/export/>
- Ultralytics ONNX integration: <https://docs.ultralytics.com/integrations/onnx/>
- ONNX Runtime docs: <https://onnxruntime.ai/docs/>
