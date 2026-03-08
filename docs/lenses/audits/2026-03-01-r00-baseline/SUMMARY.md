# Lenses Audit Summary

## Totals

- File count: **40**
- Source LOC (audited set): **9091**
- Parsed function count: **257**

## Top Files by LOC

| File | LOC |
| --- | --- |
| plugins/lenses/src/filter/lenses-filter.c | 2890 |
| plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 2028 |
| plugins/lenses/src/core/core-bridge.cpp | 585 |
| plugins/lenses/src/io/lenses-model-catalog.c | 580 |
| plugins/lenses/src/io/lenses-policy.c | 553 |
| plugins/lenses/src/ai/tracking/bytetrack-tracker.cpp | 201 |
| plugins/lenses/src/core/noop-mask-generator.cpp | 192 |
| plugins/lenses/src/ai/decode/segmentation-decoder.cpp | 188 |
| plugins/lenses/include/lenses/core/core-bridge.h | 165 |
| plugins/lenses/include/lenses/core/interfaces.hpp | 162 |

## Top Functions by Length

| Function | File | Lines | Start |
| --- | --- | --- | --- |
| InitializeSession | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 333 | 887 |
| lenses_filter_properties | plugins/lenses/src/filter/lenses-filter.c | 320 | 2528 |
| RunInference | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 232 | 1500 |
| WorkerLoop | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 184 | 1734 |
| lenses_filter_render | plugins/lenses/src/filter/lenses-filter.c | 173 | 2092 |
| lenses_submit_ai_frame | plugins/lenses/src/filter/lenses-filter.c | 144 | 1829 |
| ResizeBGRAtoRGBFloatAccelerate | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 137 | 232 |
| lenses_filter_update | plugins/lenses/src/filter/lenses-filter.c | 131 | 2294 |
| lenses_apply_runtime_config | plugins/lenses/src/filter/lenses-filter.c | 116 | 1302 |
| ByteTrackTracker::Update | plugins/lenses/src/ai/tracking/bytetrack-tracker.cpp | 114 | 86 |
| lenses_build_debug_overlay | plugins/lenses/src/filter/lenses-filter.c | 110 | 902 |
| InitializeIoBinding | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 102 | 1221 |

## Top Module Include Edges

| From | To | Count |
| --- | --- | --- |
| src:ai | system | 43 |
| include:core | system | 20 |
| src:io | system | 20 |
| src:core | system | 17 |
| include:ai | system | 16 |
| other | system | 16 |
| src:ai | include:ai | 10 |
| src:filter | system | 10 |
| src:core | include:core | 8 |
| include:io | system | 6 |
| include:ai | include:core | 5 |
| include:core | include:core | 5 |

## Most Frequent Includes

| Include | Count |
| --- | --- |
| string | 11 |
| memory | 9 |
| vector | 9 |
| lenses/core/interfaces.hpp | 8 |
| cstdint | 8 |
| algorithm | 8 |
| obs-module.h | 7 |
| mutex | 6 |
| optional | 6 |
| utility | 6 |
| lenses/ai/tracking/bytetrack-tracker.hpp | 5 |
| util/platform.h | 5 |
