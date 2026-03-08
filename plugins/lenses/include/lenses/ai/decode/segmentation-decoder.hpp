#pragma once

#include "lenses/core/types.hpp"

#include <cstdint>
#include <vector>

namespace lenses::ai::decode {

struct SegmentationCandidate {
	int32_t class_id = -1;
	float confidence = 0.0f;
	lenses::core::NormalizedRect bbox_norm{};
	std::vector<float> coeffs;
	uint64_t track_id = 0;
};

struct SegmentationTensors {
	uint32_t mask_dim = 0;
	uint32_t proto_width = 0;
	uint32_t proto_height = 0;
	std::vector<float> prototypes;
	std::vector<SegmentationCandidate> candidates;
};

struct SegmentationDecodeConfig {
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	float confidence_threshold = 0.25f;
	float mask_threshold = 0.5f;
	float min_area_ratio = 0.0f;
};

lenses::core::MaskFrame DecodeSegmentationMasks(const SegmentationTensors &tensors,
						const SegmentationDecodeConfig &config,
						uint64_t frame_id,
						uint64_t timestamp_ns);

} // namespace lenses::ai::decode
