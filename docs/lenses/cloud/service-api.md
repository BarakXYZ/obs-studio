# Cloud Service API Contract (Draft v1)

## Request

```json
{
  "version": 1,
  "frame_id": 123,
  "timestamp_ns": 123456789,
  "source": {
    "width": 1920,
    "height": 1080
  },
  "image": {
    "encoding": "rgba8",
    "data": "<opaque transport payload>"
  },
  "runtime": {
    "model": "model-id",
    "provider": "cloud"
  }
}
```

## Response

```json
{
  "version": 1,
  "frame_id": 123,
  "latency_ms": 31.4,
  "instances": [
    {
      "track_id": 1001,
      "class_id": 0,
      "confidence": 0.94,
      "bbox_norm": {"x": 0.2, "y": 0.1, "width": 0.3, "height": 0.4},
      "mask_ref": "opaque-mask-ref"
    }
  ],
  "class_union_masks": {
    "0": "opaque-mask-ref"
  }
}
```

## Adapter Rules

- Timeout budget: `RuntimeConfig.cloud_timeout_ms`.
- On timeout/error: fallback to local backend in same frame pipeline.
- Contract invariants:
  - `frame_id` round-trips unchanged.
  - `class_union_masks` and `instances` remain deterministic for same input+model.
  - Policy/compositor code is backend-agnostic.
