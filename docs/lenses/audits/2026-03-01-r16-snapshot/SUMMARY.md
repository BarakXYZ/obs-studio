# Lenses Audit Summary

## Totals

- File count: **61**
- Source LOC (audited set): **9583**
- Parsed function count: **265**

## Top Files by LOC

| File | LOC |
| --- | --- |
| plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 1105 |
| plugins/lenses/src/core/core-bridge.cpp | 585 |
| plugins/lenses/src/filter/host/lenses-filter-properties.c | 581 |
| plugins/lenses/src/io/lenses-model-catalog.c | 580 |
| plugins/lenses/src/io/lenses-policy.c | 553 |
| plugins/lenses/src/filter/host/lenses-filter-runtime-config.c | 509 |
| plugins/lenses/src/filter/host/lenses-filter-policy-graph.c | 480 |
| plugins/lenses/src/filter/lenses-filter.c | 427 |
| plugins/lenses/src/ai/ort/onnx-mask-generator-preprocess.cpp | 380 |
| plugins/lenses/src/filter/host/lenses-filter-internal.h | 271 |

## Top Functions by Length

| Function | File | Lines | Start |
| --- | --- | --- | --- |
| lenses_filter_properties | plugins/lenses/src/filter/host/lenses-filter-properties.c | 319 | 263 |
| RunInference | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 239 | 570 |
| WorkerLoop | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 184 | 811 |
| lenses_submit_ai_frame | plugins/lenses/src/filter/host/lenses-filter-ai-submit.c | 144 | 76 |
| ConfigureExecutionProviders | plugins/lenses/src/ai/ort/onnx-mask-generator-provider.cpp | 141 | 23 |
| ResizeBGRAtoRGBFloatAccelerate | plugins/lenses/src/ai/ort/onnx-mask-generator-preprocess.cpp | 138 | 217 |
| lenses_filter_update | plugins/lenses/src/filter/lenses-filter.c | 131 | 216 |
| InitializeSession | plugins/lenses/src/ai/ort/onnx-mask-generator.cpp | 119 | 391 |
| lenses_apply_runtime_config | plugins/lenses/src/filter/host/lenses-filter-runtime-config.c | 116 | 394 |
| ByteTrackTracker::Update | plugins/lenses/src/ai/tracking/bytetrack-tracker.cpp | 114 | 86 |
| lenses_build_debug_overlay | plugins/lenses/src/filter/host/lenses-filter-policy-graph.c | 110 | 218 |
| DecodeDetections | plugins/lenses/src/ai/ort/onnx-mask-generator-layout.cpp | 105 | 92 |

## Top Module Include Edges

| From | To | Count |
| --- | --- | --- |
| src:ai | system | 80 |
| src:filter | system | 31 |
| include:core | system | 20 |
| src:io | system | 20 |
| src:ai | src:ai | 18 |
| src:core | system | 17 |
| include:ai | system | 16 |
| other | system | 16 |
| src:ai | include:ai | 11 |
| src:core | include:core | 8 |
| src:filter | src:filter | 8 |
| src:ai | include:core | 7 |

## Most Frequent Includes

| Include | Count |
| --- | --- |
| string | 15 |
| vector | 14 |
| lenses/core/interfaces.hpp | 12 |
| cstdint | 12 |
| algorithm | 12 |
| obs-module.h | 11 |
| memory | 10 |
| util/bmem.h | 9 |
| inttypes.h | 9 |
| filter/host/lenses-filter-internal.h | 8 |
| util/platform.h | 7 |
| mutex | 6 |
