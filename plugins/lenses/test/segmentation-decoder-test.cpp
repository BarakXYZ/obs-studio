#include "lenses/ai/decode/segmentation-decoder.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

size_t ActivePixels(const lenses::core::BinaryMask &mask)
{
	size_t active = 0;
	for (uint8_t value : mask.data) {
		if (value > 0)
			active++;
	}
	return active;
}

bool Expect(bool condition, const std::string &message)
{
	if (condition)
		return true;

	std::cerr << "FAIL: " << message << std::endl;
	return false;
}

bool TestSingleInstanceDecode()
{
	lenses::ai::decode::SegmentationTensors tensors{};
	tensors.mask_dim = 1;
	tensors.proto_width = 4;
	tensors.proto_height = 4;
	tensors.prototypes.assign(16, 3.0f);

	lenses::ai::decode::SegmentationCandidate candidate{};
	candidate.class_id = 0;
	candidate.confidence = 0.95f;
	candidate.bbox_norm = {0.0f, 0.0f, 1.0f, 1.0f};
	candidate.coeffs = {1.0f};
	tensors.candidates.push_back(candidate);

	lenses::ai::decode::SegmentationDecodeConfig config{};
	config.source_width = 1920;
	config.source_height = 1080;
	config.confidence_threshold = 0.25f;
	config.mask_threshold = 0.5f;
	config.min_area_ratio = 0.1f;

	auto frame = lenses::ai::decode::DecodeSegmentationMasks(tensors, config, 42, 1234);
	if (!Expect(frame.instances.size() == 1, "single instance should decode"))
		return false;
	if (!Expect(frame.class_union_masks.count(0) == 1, "class union for class 0 expected"))
		return false;
	if (!Expect(frame.mask_bitmaps.size() == 2, "instance + class-union masks expected"))
		return false;

	const auto handle = frame.instances.front().mask_handle.value;
	const auto it = frame.mask_bitmaps.find(handle);
	if (!Expect(it != frame.mask_bitmaps.end(), "instance mask handle must resolve"))
		return false;

	return Expect(ActivePixels(it->second) == 16, "full bbox should keep all pixels");
}

bool TestAreaThresholdFilter()
{
	lenses::ai::decode::SegmentationTensors tensors{};
	tensors.mask_dim = 1;
	tensors.proto_width = 4;
	tensors.proto_height = 4;
	tensors.prototypes.assign(16, 3.0f);

	lenses::ai::decode::SegmentationCandidate candidate{};
	candidate.class_id = 0;
	candidate.confidence = 0.95f;
	candidate.bbox_norm = {0.0f, 0.0f, 0.5f, 0.5f};
	candidate.coeffs = {1.0f};
	tensors.candidates.push_back(candidate);

	lenses::ai::decode::SegmentationDecodeConfig config{};
	config.source_width = 1280;
	config.source_height = 720;
	config.confidence_threshold = 0.25f;
	config.mask_threshold = 0.5f;
	config.min_area_ratio = 0.5f;

	auto frame = lenses::ai::decode::DecodeSegmentationMasks(tensors, config, 43, 2234);
	if (!Expect(frame.instances.empty(), "area threshold should reject tiny region"))
		return false;
	return Expect(frame.class_union_masks.empty(), "no class union expected after rejection");
}

bool TestClassUnionMerge()
{
	lenses::ai::decode::SegmentationTensors tensors{};
	tensors.mask_dim = 1;
	tensors.proto_width = 4;
	tensors.proto_height = 4;
	tensors.prototypes.assign(16, 3.0f);

	lenses::ai::decode::SegmentationCandidate left{};
	left.class_id = 2;
	left.confidence = 0.90f;
	left.bbox_norm = {0.0f, 0.0f, 0.5f, 1.0f};
	left.coeffs = {1.0f};
	tensors.candidates.push_back(left);

	lenses::ai::decode::SegmentationCandidate right{};
	right.class_id = 2;
	right.confidence = 0.92f;
	right.bbox_norm = {0.5f, 0.0f, 0.5f, 1.0f};
	right.coeffs = {1.0f};
	tensors.candidates.push_back(right);

	lenses::ai::decode::SegmentationDecodeConfig config{};
	config.source_width = 1280;
	config.source_height = 720;
	config.confidence_threshold = 0.25f;
	config.mask_threshold = 0.5f;

	auto frame = lenses::ai::decode::DecodeSegmentationMasks(tensors, config, 44, 3234);
	if (!Expect(frame.instances.size() == 2, "expected two decoded instances"))
		return false;

	const auto class_it = frame.class_union_masks.find(2);
	if (!Expect(class_it != frame.class_union_masks.end(), "union mask for class 2 expected"))
		return false;

	const auto union_mask_it = frame.mask_bitmaps.find(class_it->second.value);
	if (!Expect(union_mask_it != frame.mask_bitmaps.end(), "union mask handle must resolve"))
		return false;

	return Expect(ActivePixels(union_mask_it->second) == 16,
		      "left+right regions should merge into full-width class union");
}

} // namespace

int main()
{
	bool ok = true;
	ok = TestSingleInstanceDecode() && ok;
	ok = TestAreaThresholdFilter() && ok;
	ok = TestClassUnionMerge() && ok;

	if (!ok)
		return 1;

	std::cout << "segmentation-decoder-test: PASS" << std::endl;
	return 0;
}
