# Lenses Model Workflow

This guide defines the supported model packaging workflow for the Lenses mask engine.

## Goals

- Keep model build/export reproducible across contributors and CI.
- Keep runtime plugin integration independent from Python.
- Standardize metadata and class maps for deterministic policy behavior.

## Workflow

1. Export a segmentation model to ONNX using Ultralytics tooling.
2. Package the model into Lenses v1 package files (`model.onnx`, `metadata.json`, `class-map.json`).
3. Validate package integrity before runtime use.
4. Load model path via Lenses filter properties.

## Commands

```bash
python3 tools/lenses-models/export_ultralytics_onnx.py \
  --model yolo11n-seg.pt \
  --output-dir /tmp/lenses-models/yolo11n-seg \
  --imgsz 640 --dynamic --simplify

python3 tools/lenses-models/validate_model_package.py \
  --package-dir /tmp/lenses-models/yolo11n-seg
```

For COCO policy authoring, canonical class map is generated with:

```bash
python3 tools/lenses-models/generate_coco_class_map.py \
  --output tools/lenses-models/class-maps/coco80.json
```

## Runtime Notes

- Runtime inference in OBS remains ONNX Runtime-based and async in plugin code.
- Ultralytics is used for export/tooling only, not render-thread inference.
- Segmentation task models are required for mask-first pipelines.

## References

- Ultralytics Export: <https://docs.ultralytics.com/modes/export/>
- Ultralytics ONNX: <https://docs.ultralytics.com/integrations/onnx/>
- Ultralytics Tracking: <https://docs.ultralytics.com/modes/track/>
- ONNX Runtime: <https://onnxruntime.ai/docs/>
