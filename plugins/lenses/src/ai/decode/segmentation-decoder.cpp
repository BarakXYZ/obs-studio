#include "lenses/ai/decode/segmentation-decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace lenses::ai::decode {

namespace {

constexpr float kMinBoxDimension = 1e-6f;

float Clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

lenses::core::NormalizedRect ClampRect(lenses::core::NormalizedRect rect)
{
	rect.x = Clamp01(rect.x);
	rect.y = Clamp01(rect.y);
	rect.width = Clamp01(rect.width);
	rect.height = Clamp01(rect.height);
	if (rect.x + rect.width > 1.0f)
		rect.width = std::max(0.0f, 1.0f - rect.x);
	if (rect.y + rect.height > 1.0f)
		rect.height = std::max(0.0f, 1.0f - rect.y);
	return rect;
}

bool ValidateTensorShape(const SegmentationTensors &tensors)
{
	if (tensors.mask_dim == 0 || tensors.proto_width == 0 || tensors.proto_height == 0)
		return false;

	const size_t expected_count =
		(size_t)tensors.mask_dim * (size_t)tensors.proto_width * (size_t)tensors.proto_height;
	return tensors.prototypes.size() == expected_count;
}

uint64_t BuildMaskHandle(uint64_t frame_id, uint64_t local_index)
{
	return (frame_id << 20U) | (local_index & 0xFFFFFU);
}

void CropMaskToRect(lenses::core::BinaryMask &mask, const lenses::core::NormalizedRect &bbox)
{
	if (mask.width == 0 || mask.height == 0 || mask.data.empty())
		return;

	const uint32_t x0 = (uint32_t)std::floor((double)bbox.x * (double)mask.width);
	const uint32_t y0 = (uint32_t)std::floor((double)bbox.y * (double)mask.height);
	const uint32_t x1 =
		(uint32_t)std::ceil((double)(bbox.x + bbox.width) * (double)mask.width);
	const uint32_t y1 =
		(uint32_t)std::ceil((double)(bbox.y + bbox.height) * (double)mask.height);

	for (uint32_t y = 0; y < mask.height; ++y) {
		for (uint32_t x = 0; x < mask.width; ++x) {
			if (x < x0 || x >= x1 || y < y0 || y >= y1)
				mask.data[(size_t)y * mask.width + x] = 0;
		}
	}
}

float ComputeAreaRatio(const lenses::core::BinaryMask &mask)
{
	if (mask.width == 0 || mask.height == 0 || mask.data.empty())
		return 0.0f;

	size_t active = 0;
	for (uint8_t value : mask.data) {
		if (value > 0)
			active++;
	}
	const size_t total = (size_t)mask.width * (size_t)mask.height;
	if (total == 0)
		return 0.0f;
	return (float)active / (float)total;
}

lenses::core::BinaryMask DecodeCandidate(const SegmentationTensors &tensors,
					 const SegmentationCandidate &candidate,
					 float mask_threshold)
{
	lenses::core::BinaryMask mask{};
	mask.width = tensors.proto_width;
	mask.height = tensors.proto_height;
	mask.data.assign((size_t)mask.width * (size_t)mask.height, 0);

	const size_t plane_area = (size_t)tensors.proto_width * (size_t)tensors.proto_height;
	for (size_t index = 0; index < plane_area; ++index) {
		double value = 0.0;
		for (uint32_t channel = 0; channel < tensors.mask_dim; ++channel) {
			const size_t proto_index = (size_t)channel * plane_area + index;
			value += (double)candidate.coeffs[channel] * (double)tensors.prototypes[proto_index];
		}

		const double probability = 1.0 / (1.0 + std::exp(-value));
		mask.data[index] = probability >= (double)mask_threshold ? (uint8_t)255 : (uint8_t)0;
	}

	CropMaskToRect(mask, ClampRect(candidate.bbox_norm));
	return mask;
}

void MergeMaskInto(lenses::core::BinaryMask &destination, const lenses::core::BinaryMask &source)
{
	if (destination.width != source.width || destination.height != source.height)
		return;

	for (size_t i = 0; i < destination.data.size() && i < source.data.size(); ++i)
		destination.data[i] = (destination.data[i] > 0 || source.data[i] > 0) ? (uint8_t)255 : (uint8_t)0;
}

} // namespace

lenses::core::MaskFrame DecodeSegmentationMasks(const SegmentationTensors &tensors,
						const SegmentationDecodeConfig &config,
						uint64_t frame_id,
						uint64_t timestamp_ns)
{
	lenses::core::MaskFrame frame{};
	frame.frame_id = frame_id;
	frame.source_width = config.source_width;
	frame.source_height = config.source_height;
	frame.timestamp_ns = timestamp_ns;

	if (!ValidateTensorShape(tensors))
		return frame;

	uint64_t handle_index = 1;
	std::unordered_map<int32_t, lenses::core::BinaryMask> class_union_masks;

	for (const auto &candidate : tensors.candidates) {
		if ((size_t)tensors.mask_dim != candidate.coeffs.size())
			continue;
		if (candidate.confidence < config.confidence_threshold)
			continue;

		const lenses::core::NormalizedRect bbox = ClampRect(candidate.bbox_norm);
		if (bbox.width <= kMinBoxDimension || bbox.height <= kMinBoxDimension)
			continue;

		lenses::core::BinaryMask mask = DecodeCandidate(tensors, candidate, config.mask_threshold);
		const float area_ratio = ComputeAreaRatio(mask);
		if (area_ratio < config.min_area_ratio)
			continue;

		lenses::core::MaskInstance instance{};
		instance.track_id = candidate.track_id;
		instance.class_id = candidate.class_id;
		instance.confidence = candidate.confidence;
		instance.bbox_norm = bbox;
		const uint64_t instance_handle = BuildMaskHandle(frame_id, handle_index++);
		instance.mask_handle = lenses::core::MaskHandle{instance_handle};
		instance.timestamp_ns = timestamp_ns;
		frame.instances.push_back(std::move(instance));
		frame.mask_bitmaps.emplace(instance_handle, mask);
		const auto bitmap_it = frame.mask_bitmaps.find(instance_handle);
		if (bitmap_it == frame.mask_bitmaps.end())
			continue;
		const lenses::core::BinaryMask &instance_mask = bitmap_it->second;

		auto union_it = class_union_masks.find(candidate.class_id);
		if (union_it == class_union_masks.end()) {
			class_union_masks.emplace(candidate.class_id, instance_mask);
		} else {
			MergeMaskInto(union_it->second, instance_mask);
		}
	}

	for (auto &[class_id, union_mask] : class_union_masks) {
		const uint64_t union_handle = BuildMaskHandle(frame_id, handle_index++);
		frame.class_union_masks[class_id] = lenses::core::MaskHandle{union_handle};
		frame.mask_bitmaps.emplace(union_handle, std::move(union_mask));
	}

	return frame;
}

} // namespace lenses::ai::decode
