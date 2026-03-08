#pragma once

#include "filter/invert/lenses-invert-hue-qualifier.h"

#include <array>
#include <cstdint>

namespace lenses::filter::invert {

class HueQualifier {
public:
	void Configure(const lenses_invert_hue_range_config &config) noexcept;

	bool HasActiveBands() const noexcept { return active_; }
	bool ExcludeMode() const noexcept { return exclude_mode_; }

	const std::array<uint8_t, 256> &SelectionLookup() const noexcept
	{
		return selection_lookup_;
	}
	const std::array<uint8_t, 256> &AppliedLookup() const noexcept
	{
		return applied_lookup_;
	}

	float SelectionMembershipForDegrees(float hue_degrees) const noexcept;

private:
	struct Band {
		bool enabled = false;
		float center_degrees = 0.0f;
		float width_degrees = 0.0f;
		float softness_degrees = 0.0f;
	};

	static float Clamp(float value, float min_value, float max_value) noexcept;
	static float NormalizeDegrees(float degrees) noexcept;
	static float CircularDistanceDegrees(float a, float b) noexcept;
	static float SmoothStep(float edge0, float edge1, float x) noexcept;
	static float BandMembership(float hue_degrees, const Band &band) noexcept;
	static float QuantizedHueDegrees(uint8_t hue_bin) noexcept;

	float SelectionMembershipNoClamp(float hue_degrees) const noexcept;

	bool active_ = false;
	bool exclude_mode_ = true;
	std::array<Band, LENSES_INVERT_HUE_RANGE_MAX_BANDS> bands_{};
	std::array<uint8_t, 256> selection_lookup_{};
	std::array<uint8_t, 256> applied_lookup_{};
};

} // namespace lenses::filter::invert

