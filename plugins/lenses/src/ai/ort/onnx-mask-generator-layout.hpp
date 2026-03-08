#pragma once

#include "lenses/ai/decode/segmentation-decoder.hpp"

#include <cstdint>
#include <vector>

namespace lenses::ai::ort::detail {

struct DetectionLayout {
	size_t candidate_count = 0;
	size_t feature_count = 0;
	bool channel_first = true;
};

bool BuildDetectionLayout(const std::vector<int64_t> &shape, uint32_t mask_dim,
			 size_t max_detection_candidates,
			 size_t max_detection_features, DetectionLayout &layout);

bool InferProtoLayoutFromElementCount(size_t proto_elements, uint32_t input_width,
				    uint32_t input_height,
				    size_t max_detection_features,
				    uint32_t &out_mask_dim,
				    uint32_t &out_proto_width,
				    uint32_t &out_proto_height,
				    bool &out_proto_channel_first);

bool InferDetectionLayoutFromElementCount(size_t detection_elements,
				 uint32_t current_mask_dim,
				 uint32_t default_coco_class_count,
				 size_t max_detection_features,
				 size_t max_detection_candidates,
				 DetectionLayout &layout);

void DecodeDetections(const float *detections, const DetectionLayout &layout,
		      uint32_t mask_dim, uint32_t input_width, uint32_t input_height,
		      float confidence_threshold, float nms_iou_threshold,
		      size_t max_detections,
		      std::vector<lenses::ai::decode::SegmentationCandidate> &out);

} // namespace lenses::ai::ort::detail
