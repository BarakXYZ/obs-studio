#include "ai/ort/onnx-mask-generator-layout.hpp"
#include "ai/ort/onnx-mask-generator-preprocess.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lenses::ai::ort::detail {

bool BuildDetectionLayout(const std::vector<int64_t> &shape, uint32_t mask_dim,
			 size_t max_detection_candidates,
			 size_t max_detection_features, DetectionLayout &layout)
{
	if (shape.size() != 3)
		return false;

	const size_t a = (size_t)std::max<int64_t>(1, shape[1]);
	const size_t b = (size_t)std::max<int64_t>(1, shape[2]);
	if (a == 0 || b == 0)
		return false;

	const size_t min_dim = std::min(a, b);
	const size_t max_dim = std::max(a, b);
	if (min_dim <= 4U + (size_t)mask_dim)
		return false;
	if (max_dim > max_detection_candidates || min_dim > max_detection_features)
		return false;

	layout.feature_count = min_dim;
	layout.candidate_count = max_dim;
	layout.channel_first = (a == min_dim);
	return true;
}

bool InferProtoLayoutFromElementCount(size_t proto_elements, uint32_t input_width,
				    uint32_t input_height,
				    size_t max_detection_features,
				    uint32_t &out_mask_dim,
				    uint32_t &out_proto_width,
				    uint32_t &out_proto_height,
				    bool &out_proto_channel_first)
{
	const std::array<uint32_t, 5> strides = {4, 8, 16, 2, 1};
	for (uint32_t stride : strides) {
		const uint32_t inferred_width = std::max<uint32_t>(1, input_width / stride);
		const uint32_t inferred_height = std::max<uint32_t>(1, input_height / stride);
		const size_t area = (size_t)inferred_width * inferred_height;
		if (area == 0 || (proto_elements % area) != 0)
			continue;

		const size_t inferred_mask_dim = proto_elements / area;
		if (inferred_mask_dim == 0 || inferred_mask_dim > max_detection_features)
			continue;

		out_mask_dim = (uint32_t)inferred_mask_dim;
		out_proto_width = inferred_width;
		out_proto_height = inferred_height;
		out_proto_channel_first = true;
		return true;
	}

	return false;
}

bool InferDetectionLayoutFromElementCount(size_t detection_elements,
				 uint32_t current_mask_dim,
				 uint32_t default_coco_class_count,
				 size_t max_detection_features,
				 size_t max_detection_candidates,
				 DetectionLayout &layout)
{
	if (current_mask_dim == 0)
		return false;

	const size_t feature_count =
		4U + (size_t)default_coco_class_count + (size_t)current_mask_dim;
	if (feature_count == 0 || feature_count > max_detection_features)
		return false;
	if (detection_elements == 0 || (detection_elements % feature_count) != 0)
		return false;

	const size_t candidate_count = detection_elements / feature_count;
	if (candidate_count == 0 || candidate_count > max_detection_candidates)
		return false;

	layout.feature_count = feature_count;
	layout.candidate_count = candidate_count;
	layout.channel_first = true;
	return true;
}

void DecodeDetections(const float *detections, const DetectionLayout &layout,
		      uint32_t mask_dim, uint32_t input_width, uint32_t input_height,
		      float confidence_threshold, float nms_iou_threshold,
		      size_t max_detections,
		      std::vector<lenses::ai::decode::SegmentationCandidate> &out)
{
	const size_t feature_count = layout.feature_count;
	const size_t candidate_count = layout.candidate_count;
	if (feature_count <= (size_t)(4U + mask_dim))
		return;

	const size_t num_classes = feature_count - 4U - mask_dim;
	if (num_classes == 0)
		return;

	out.clear();
	out.reserve(std::min(candidate_count, max_detections));

	auto get_value = [&](size_t candidate, size_t feature) -> float {
		if (layout.channel_first)
			return detections[feature * candidate_count + candidate];
		return detections[candidate * feature_count + feature];
	};

	for (size_t candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
		float best_class_score = 0.0f;
		int32_t best_class_id = -1;
		for (size_t class_idx = 0; class_idx < num_classes; ++class_idx) {
			const float score = get_value(candidate_index, 4U + class_idx);
			if (score > best_class_score) {
				best_class_score = score;
				best_class_id = (int32_t)class_idx;
			}
		}

		if (best_class_id < 0 || best_class_score < confidence_threshold)
			continue;

		const float xc = get_value(candidate_index, 0);
		const float yc = get_value(candidate_index, 1);
		const float w = get_value(candidate_index, 2);
		const float h = get_value(candidate_index, 3);

		float nx = xc;
		float ny = yc;
		float nw = w;
		float nh = h;
		if (std::max({std::fabs(xc), std::fabs(yc), std::fabs(w), std::fabs(h)}) > 2.0f) {
			nx = xc / (float)input_width;
			ny = yc / (float)input_height;
			nw = w / (float)input_width;
			nh = h / (float)input_height;
		}

		lenses::core::NormalizedRect bbox{};
		bbox.x = Clamp01(nx - nw * 0.5f);
		bbox.y = Clamp01(ny - nh * 0.5f);
		bbox.width = Clamp01(nw);
		bbox.height = Clamp01(nh);
		if (bbox.x + bbox.width > 1.0f)
			bbox.width = std::max(0.0f, 1.0f - bbox.x);
		if (bbox.y + bbox.height > 1.0f)
			bbox.height = std::max(0.0f, 1.0f - bbox.y);
		if (bbox.width <= 0.0f || bbox.height <= 0.0f)
			continue;

		lenses::ai::decode::SegmentationCandidate det{};
		det.class_id = best_class_id;
		det.confidence = best_class_score;
		det.bbox_norm = bbox;
		det.coeffs.resize(mask_dim);
		for (uint32_t k = 0; k < mask_dim; ++k)
			det.coeffs[k] = get_value(candidate_index, 4U + num_classes + k);

		out.push_back(std::move(det));
	}

	std::sort(out.begin(), out.end(), [](const auto &lhs, const auto &rhs) {
		if (lhs.confidence != rhs.confidence)
			return lhs.confidence > rhs.confidence;
		return lhs.class_id < rhs.class_id;
	});

	std::vector<lenses::ai::decode::SegmentationCandidate> kept;
	kept.reserve(out.size());
	for (const auto &candidate : out) {
		bool suppressed = false;
		for (const auto &selected : kept) {
			if (selected.class_id != candidate.class_id)
				continue;
			if (BoxIoU(selected.bbox_norm, candidate.bbox_norm) > nms_iou_threshold) {
				suppressed = true;
				break;
			}
		}

		if (!suppressed) {
			kept.push_back(candidate);
			if (kept.size() >= max_detections)
				break;
		}
	}

	out.swap(kept);
}

} // namespace lenses::ai::ort::detail
