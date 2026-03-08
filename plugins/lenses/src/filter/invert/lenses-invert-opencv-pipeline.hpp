#pragma once

#include "filter/invert/lenses-invert-hue-qualifier.h"

#include <cstddef>
#include <cstdint>
#include <opencv2/core/mat.hpp>
#include <vector>

namespace lenses::filter::invert {

enum class OpenCvInputLayout : uint8_t {
	Bgra8 = 0,
	Luma8 = 1,
};

struct OpenCvRegionParams {
	float threshold = 0.65f;
	/*
	 * Edge softness is the user-facing feather pass applied after component
	 * selection and coverage shaping.
	 */
	float edge_softness = 0.25f;
	/*
	 * Topology softness stabilizes threshold/morphology behavior and is kept
	 * internal so edge-feather UX remains predictable.
	 */
	float topology_softness = 0.24f;
	float coverage = 0.45f;
	float luma_min = 0.0f;
	float luma_max = 1.0f;
	float saturation_min = 0.0f;
	float saturation_max = 1.0f;
	float hue_min_degrees = 0.0f;
	float hue_max_degrees = 360.0f;
	struct lenses_invert_hue_range_config hue_qualifier = {};
	float min_area_px = 96.0f;
	float min_side_px = 8.0f;
	float min_fill = 0.12f;
	float min_coverage = 0.001f;
	bool capture_hue_debug_preview = false;
};

struct OpenCvRegionStats {
	uint32_t accepted_pixels = 0;
	uint32_t accepted_components = 0;
	float accepted_coverage = 0.0f;
};

struct OpenCvRegionDebugPreview {
	bool has_hue_preview = false;
	uint32_t hue_selected_pixels = 0;
	float hue_selected_coverage = 0.0f;
	std::vector<uint8_t> hue_preview_bgra;
};

class OpenCvRegionMaskPipeline {
public:
	bool BuildMask(const uint8_t *bgra, uint32_t width, uint32_t height, uint32_t linesize,
		       const OpenCvRegionParams &params, std::vector<uint8_t> &out_mask,
		       OpenCvRegionStats &out_stats,
		       OpenCvRegionDebugPreview *out_debug_preview = nullptr,
		       std::vector<uint8_t> *out_expansion_gate = nullptr,
		       OpenCvInputLayout input_layout = OpenCvInputLayout::Bgra8) noexcept;

private:
	static float Clamp(float value, float min_value, float max_value) noexcept;
	static int OddKernelFromSoftness(float softness, int min_size, int max_size) noexcept;
	static float NormalizeHueDegrees(float degrees) noexcept;
	static int HueDegreesToOpenCv(float degrees) noexcept;
	static bool IsFullHueRange(float min_degrees, float max_degrees) noexcept;
	static void DrawHueRangeBar(std::vector<uint8_t> &preview, uint32_t width, uint32_t height,
				    const struct lenses_invert_hue_range_config &config) noexcept;
	static void BuildHuePreview(const cv::Mat &bgra_view, const cv::Mat &selection_mask,
				    const struct lenses_invert_hue_range_config &config,
				    OpenCvRegionDebugPreview *out_debug_preview) noexcept;
	bool BuildComponentLut(const cv::Mat &stats, const OpenCvRegionParams &params,
		       std::vector<uint8_t> &out_keep) const;

	cv::Mat bgr_;
	cv::Mat hsv_;
	cv::Mat hue_channel_;
	cv::Mat saturation_channel_;
	cv::Mat grayscale_;
	cv::Mat blurred_;
	cv::Mat seed_mask_;
	cv::Mat expansion_gate_mask_;
	cv::Mat luma_gate_;
	cv::Mat strict_luma_gate_;
	cv::Mat saturation_gate_;
	cv::Mat hue_gate_;
	cv::Mat hue_gate_low_;
	cv::Mat hue_gate_high_;
	cv::Mat hue_gate_legacy_;
	cv::Mat hue_selection_gate_;
	cv::Mat closed_mask_;
	cv::Mat opened_mask_;
	cv::Mat labels_;
	cv::Mat stats_;
	cv::Mat centroids_;
	cv::Mat selected_mask_;
	cv::Mat softened_mask_;
	cv::Mat coverage_float_;
};

} // namespace lenses::filter::invert
