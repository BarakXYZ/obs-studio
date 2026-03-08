# Lenses Model Package Spec v1

## Directory Layout

A valid package directory contains:

- `model.onnx`
- `metadata.json`
- `class-map.json`

## `metadata.json`

Required keys:

- `version`: integer schema version (current: `1`).
- `lenses_schema`: string (`"lenses-model-v1"`).
- `model`: object with:
  - `file`: model filename (`model.onnx` by convention).
  - `sha256`: optional hash of model file.
  - `imgsz`, `batch`, `dynamic`, `opset`: export metadata.
- `class_map_file`: file name for class map JSON.
- `class_count`: number of classes in class map.

Optional keys can include export provenance (`source_model`, `ultralytics_export`) and timestamps.

## `class-map.json`

Required shape:

```json
{
  "version": 1,
  "dataset": "coco80",
  "classes": [
    {"id": 0, "name": "person"}
  ]
}
```

Constraints:

- `classes` must be dense id mapping from `0..N-1`.
- `metadata.class_count` must equal `classes.length`.
- Class names must be non-empty strings.

## Validation

Use:

```bash
python3 tools/lenses-models/validate_model_package.py --package-dir <dir>
```

Use `--strict-onnx` when `onnx` Python package is available and structural model verification is required.
