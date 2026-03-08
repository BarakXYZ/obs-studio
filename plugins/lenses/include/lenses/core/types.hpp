#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lenses::core {

enum class RegionMode : uint8_t {
	Include = 0,
	Exclude = 1,
};

enum class BlendMode : uint8_t {
	Replace = 0,
	AlphaMix = 1,
	Add = 2,
	Multiply = 3,
};

struct NormalizedRect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

struct MaskHandle {
	uint64_t value = 0;

	[[nodiscard]] bool Valid() const noexcept { return value != 0; }
};

struct BinaryMask {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> data;
};

struct MaskInstance {
	uint64_t track_id = 0;
	int32_t class_id = -1;
	float confidence = 0.0f;
	NormalizedRect bbox_norm{};
	MaskHandle mask_handle{};
	uint64_t timestamp_ns = 0;
};

struct MaskFrame {
	uint64_t frame_id = 0;
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	uint64_t timestamp_ns = 0;
	std::vector<MaskInstance> instances;
	std::unordered_map<int32_t, MaskHandle> class_union_masks;
	std::unordered_map<uint64_t, BinaryMask> mask_bitmaps;
	double latency_ms = 0.0;
};

struct RuleSelector {
	std::vector<int32_t> class_ids;
	std::vector<std::string> class_names;
	std::vector<uint64_t> track_ids;
	float min_confidence = 0.0f;
	float min_area = 0.0f;
	float max_area = 1.0f;
};

struct Rule {
	std::string id;
	bool enabled = true;
	int priority = 0;
	RuleSelector selector;
	RegionMode region_mode = RegionMode::Include;
	std::string filter_chain_id;
	BlendMode blend_mode = BlendMode::Replace;
	float opacity = 1.0f;
};

struct ExecutionPlan {
	std::vector<Rule> ordered_rules;
	std::optional<Rule> default_rule;
	uint64_t revision = 0;
};

} // namespace lenses::core
