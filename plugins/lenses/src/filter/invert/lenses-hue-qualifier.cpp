#include "filter/invert/lenses-hue-qualifier.hpp"

#include <algorithm>
#include <cmath>

namespace lenses::filter::invert {

float HueQualifier::Clamp(float value, float min_value, float max_value) noexcept
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

float HueQualifier::NormalizeDegrees(float degrees) noexcept
{
	if (!std::isfinite(degrees))
		return 0.0f;

	float normalized = std::fmod(degrees, 360.0f);
	if (normalized < 0.0f)
		normalized += 360.0f;
	if (normalized >= 360.0f)
		normalized -= 360.0f;
	return normalized;
}

float HueQualifier::CircularDistanceDegrees(float a, float b) noexcept
{
	const float na = NormalizeDegrees(a);
	const float nb = NormalizeDegrees(b);
	const float delta = std::fabs(na - nb);
	return std::min(delta, 360.0f - delta);
}

float HueQualifier::SmoothStep(float edge0, float edge1, float x) noexcept
{
	if (edge1 <= edge0)
		return x < edge0 ? 0.0f : 1.0f;

	const float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float HueQualifier::BandMembership(float hue_degrees, const Band &band) noexcept
{
	if (!band.enabled)
		return 0.0f;

	const float full_width = Clamp(band.width_degrees, 0.0f, 360.0f);
	if (full_width >= 359.5f)
		return 1.0f;

	const float feather = Clamp(band.softness_degrees, 0.0f, 180.0f);
	const float core_half = 0.5f * full_width;
	const float distance = CircularDistanceDegrees(hue_degrees, band.center_degrees);
	if (distance <= core_half)
		return 1.0f;
	if (feather <= 0.0001f)
		return 0.0f;
	if (distance >= (core_half + feather))
		return 0.0f;

	const float falloff = 1.0f - SmoothStep(core_half, core_half + feather, distance);
	return Clamp(falloff, 0.0f, 1.0f);
}

float HueQualifier::QuantizedHueDegrees(uint8_t hue_bin) noexcept
{
	/* OpenCV HSV hue channel is encoded as 0..179 for 0..360 degrees. */
	return (float)hue_bin * 2.0f;
}

float HueQualifier::SelectionMembershipNoClamp(float hue_degrees) const noexcept
{
	float membership = 0.0f;
	for (const Band &band : bands_)
		membership = std::max(membership, BandMembership(hue_degrees, band));
	return membership;
}

void HueQualifier::Configure(const lenses_invert_hue_range_config &config) noexcept
{
	active_ = false;
	exclude_mode_ = config.mode == LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE;
	bands_.fill(Band{});
	selection_lookup_.fill(0);
	applied_lookup_.fill(exclude_mode_ ? 255U : 0U);

	if (!config.enabled)
		return;

	for (size_t i = 0; i < bands_.size(); ++i) {
		const lenses_invert_hue_range_band &src = config.bands[i];
		Band &dst = bands_[i];
		dst.enabled = src.enabled != 0;
		dst.center_degrees = NormalizeDegrees(src.center_degrees);
		dst.width_degrees = Clamp(src.width_degrees, 0.0f, 360.0f);
		dst.softness_degrees = Clamp(src.softness_degrees, 0.0f, 180.0f);
		if (dst.enabled && dst.width_degrees > 0.0001f)
			active_ = true;
	}
	if (!active_)
		return;

	for (uint16_t h = 0; h < 180; ++h) {
		const float degrees = QuantizedHueDegrees((uint8_t)h);
		const float selection_membership = SelectionMembershipNoClamp(degrees);
		const uint8_t selection_u8 =
			(uint8_t)std::lround(Clamp(selection_membership, 0.0f, 1.0f) * 255.0f);
		selection_lookup_[h] = selection_u8;

		const float applied_membership =
			exclude_mode_ ? (1.0f - selection_membership) : selection_membership;
		applied_lookup_[h] =
			(uint8_t)std::lround(Clamp(applied_membership, 0.0f, 1.0f) * 255.0f);
	}
	for (uint16_t h = 180; h < 256; ++h) {
		selection_lookup_[h] = 0;
		applied_lookup_[h] = exclude_mode_ ? 255U : 0U;
	}
}

float HueQualifier::SelectionMembershipForDegrees(float hue_degrees) const noexcept
{
	if (!active_)
		return 0.0f;
	return Clamp(SelectionMembershipNoClamp(hue_degrees), 0.0f, 1.0f);
}

} // namespace lenses::filter::invert

