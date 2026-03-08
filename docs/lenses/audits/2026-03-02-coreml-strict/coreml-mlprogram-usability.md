# CoreML Strict-Mode Usability Audit

- generated_at_utc: 2026-03-02T08:38:02.594955+00:00
- source: `onnxruntime.tools.check_onnx_model_mobile_usability`
- policy: strict mode requires `session.disable_cpu_ep_fallback=1`; any CPU partition causes startup failure

| Model | MLProgram Coverage (As-Is) | As-Is Recommended | MLProgram Coverage (Fixed-Shape Simulation) | Fixed-Shape Recommended | Unsupported Ops (MLProgram) |
| --- | ---: | --- | ---: | --- | --- |
| yolo11n-seg-coco80 | 353/355 | NO | n/a | n/a | `n/a` |
| yolo11s-seg-coco80 | 33/455 | NO | 428/455 | NO | `ai.onnx:ConstantOfShape,ai.onnx:Expand,ai.onnx:Gather,ai.onnx:Range` |
| yolo11m-seg-coco80 | 33/540 | NO | 513/540 | NO | `ai.onnx:ConstantOfShape,ai.onnx:Expand,ai.onnx:Gather,ai.onnx:Range` |
| yolo11l-seg-coco80 | 40/763 | NO | 733/763 | NO | `ai.onnx:ConstantOfShape,ai.onnx:Expand,ai.onnx:Gather,ai.onnx:Range` |
| yolo11x-seg-coco80 | 40/763 | NO | 733/763 | NO | `ai.onnx:ConstantOfShape,ai.onnx:Expand,ai.onnx:Gather,ai.onnx:Range` |

## Findings

1. No bundled YOLO11 tier is currently compatible with strict GPU-only CoreML execution under ONNX Runtime; strict fallback disabling fails startup for all tiers.
2. Dominant unsupported operators for MLProgram are shape/indexing related (`ConstantOfShape`, `Expand`, `Gather`, `Range`) plus model-specific constraints on `ConvTranspose`, `Slice`, and `Split` caveats.
3. Fixed-shape simulation significantly improves partition coverage (~94%), but still not 100%, so strict no-fallback runs remain invalid.

## Raw Reports

- `/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/audits/2026-03-02-coreml-strict/raw/yolo11n-seg-coco80.txt`
- `/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/audits/2026-03-02-coreml-strict/raw/yolo11s-seg-coco80.txt`
- `/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/audits/2026-03-02-coreml-strict/raw/yolo11m-seg-coco80.txt`
- `/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/audits/2026-03-02-coreml-strict/raw/yolo11l-seg-coco80.txt`
- `/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/audits/2026-03-02-coreml-strict/raw/yolo11x-seg-coco80.txt`
