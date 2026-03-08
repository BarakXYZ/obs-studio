#include "filter/invert/lenses-invert-opencv-pipeline.hpp"
#include "filter/invert/lenses-hue-qualifier.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lenses::filter::invert {

namespace {

static void HueToBgr(float hue_degrees, uint8_t &out_b, uint8_t &out_g, uint8_t &out_r)
{
	float h = std::fmod(hue_degrees, 360.0f);
	if (h < 0.0f)
		h += 360.0f;
	const float c = 1.0f;
	const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	if (h < 60.0f) {
		r = c;
		g = x;
	} else if (h < 120.0f) {
		r = x;
		g = c;
	} else if (h < 180.0f) {
		g = c;
		b = x;
	} else if (h < 240.0f) {
		g = x;
		b = c;
	} else if (h < 300.0f) {
		r = x;
		b = c;
	} else {
		r = c;
		b = x;
	}

	out_r = (uint8_t)std::lround(r * 255.0f);
	out_g = (uint8_t)std::lround(g * 255.0f);
	out_b = (uint8_t)std::lround(b * 255.0f);
}

static int BuildConnectedComponentsWithStats(const cv::Mat &binary_mask, cv::Mat &labels,
					     cv::Mat &stats, cv::Mat &centroids)
{
	constexpr int kConnectivity = 8;
	constexpr int kLabelType = CV_32S;
	/*
	 * OpenCV documents CCL_BBDT as the 8-connectivity algorithm variant for
	 * Grana's approach, with parallel support when image rows are sufficient.
	 * Use it explicitly to avoid backend-default drift across builds.
	 */
	constexpr int kCclType = cv::CCL_BBDT;
	return cv::connectedComponentsWithStats(binary_mask, labels, stats, centroids, kConnectivity,
						kLabelType, kCclType);
}

} // namespace

float OpenCvRegionMaskPipeline::Clamp(float value, float min_value, float max_value) noexcept
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

int OpenCvRegionMaskPipeline::OddKernelFromSoftness(float softness, int min_size, int max_size) noexcept
{
	if (min_size < 1)
		min_size = 1;
	if ((min_size % 2) == 0)
		min_size += 1;
	if (max_size < min_size)
		max_size = min_size;
	if ((max_size % 2) == 0)
		max_size -= 1;

	const float normalized = Clamp(softness, 0.0f, 1.0f);
	const int span = std::max(0, (max_size - min_size) / 2);
	const int steps = (int)std::lround(normalized * (float)span);
	return min_size + steps * 2;
}

float OpenCvRegionMaskPipeline::NormalizeHueDegrees(float degrees) noexcept
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

int OpenCvRegionMaskPipeline::HueDegreesToOpenCv(float degrees) noexcept
{
	const float normalized = NormalizeHueDegrees(degrees);
	const int hue = (int)std::lround(normalized * (179.0f / 360.0f));
	return std::clamp(hue, 0, 179);
}

bool OpenCvRegionMaskPipeline::IsFullHueRange(float min_degrees, float max_degrees) noexcept
{
	return std::fabs(max_degrees - min_degrees) >= 359.5f;
}

void OpenCvRegionMaskPipeline::DrawHueRangeBar(
	std::vector<uint8_t> &preview, uint32_t width, uint32_t height,
	const struct lenses_invert_hue_range_config &config) noexcept
{
	if (preview.empty() || width < 32 || height < 24)
		return;

	const uint32_t bar_height = std::clamp<uint32_t>(height / 12U, 16U, 32U);
	HueQualifier qualifier;
	qualifier.Configure(config);
	const bool has_ranges = qualifier.HasActiveBands();

	for (uint32_t x = 0; x < width; ++x) {
		const float hue_degrees =
			(width > 1) ? ((float)x * 360.0f / (float)(width - 1U)) : 0.0f;
		uint8_t b = 0;
		uint8_t g = 0;
		uint8_t r = 0;
		HueToBgr(hue_degrees, b, g, r);
		const float selection = has_ranges ? qualifier.SelectionMembershipForDegrees(hue_degrees) : 0.0f;
		for (uint32_t y = 0; y < bar_height; ++y) {
			const size_t index = ((size_t)y * (size_t)width + (size_t)x) * 4U;
			preview[index + 0U] = b;
			preview[index + 1U] = g;
			preview[index + 2U] = r;
			preview[index + 3U] = 220U;
			if (selection > 0.001f) {
				const uint8_t boost = (uint8_t)std::lround(70.0f + selection * 120.0f);
				preview[index + 0U] =
					(uint8_t)std::min<int>(255, preview[index + 0U] + boost / 2);
				preview[index + 1U] =
					(uint8_t)std::min<int>(255, preview[index + 1U] + boost / 2);
				preview[index + 2U] =
					(uint8_t)std::min<int>(255, preview[index + 2U] + boost / 2);
			}
		}
	}

	/* Draw a dark separator below the hue bar. */
	if (bar_height < height) {
		for (uint32_t x = 0; x < width; ++x) {
			const size_t index = ((size_t)bar_height * (size_t)width + (size_t)x) * 4U;
			preview[index + 0U] = 20U;
			preview[index + 1U] = 20U;
			preview[index + 2U] = 20U;
			preview[index + 3U] = 235U;
		}
	}
}

void OpenCvRegionMaskPipeline::BuildHuePreview(
	const cv::Mat &bgra_view, const cv::Mat &selection_mask,
	const struct lenses_invert_hue_range_config &config,
	OpenCvRegionDebugPreview *out_debug_preview) noexcept
{
	if (!out_debug_preview)
		return;
	out_debug_preview->has_hue_preview = false;
	out_debug_preview->hue_preview_bgra.clear();
	out_debug_preview->hue_selected_pixels = 0;
	out_debug_preview->hue_selected_coverage = 0.0f;

	if (bgra_view.empty() || selection_mask.empty() || bgra_view.cols <= 0 || bgra_view.rows <= 0)
		return;
	if (bgra_view.cols != selection_mask.cols || bgra_view.rows != selection_mask.rows)
		return;

	const uint32_t width = (uint32_t)bgra_view.cols;
	const uint32_t height = (uint32_t)bgra_view.rows;
	const size_t pixel_count = (size_t)width * (size_t)height;
	if (pixel_count == 0)
		return;

	out_debug_preview->hue_preview_bgra.resize(pixel_count * 4U);
	uint32_t selected_pixels = 0;
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t *src = bgra_view.ptr<uint8_t>((int)y);
		const uint8_t *selection = selection_mask.ptr<uint8_t>((int)y);
		uint8_t *dst = out_debug_preview->hue_preview_bgra.data() + (size_t)y * (size_t)width * 4U;
		for (uint32_t x = 0; x < width; ++x) {
			const uint8_t sb = src[x * 4U + 0U];
			const uint8_t sg = src[x * 4U + 1U];
			const uint8_t sr = src[x * 4U + 2U];
			const uint8_t sel = selection[x];
			const size_t idx = (size_t)x * 4U;
			if (sel > 0) {
				dst[idx + 0U] = sb;
				dst[idx + 1U] = sg;
				dst[idx + 2U] = sr;
				dst[idx + 3U] = (uint8_t)std::max<int>(96, sel);
				selected_pixels++;
			} else {
				const uint8_t luma =
					(uint8_t)(((uint16_t)sb * 29U + (uint16_t)sg * 150U +
						   (uint16_t)sr * 77U + 128U) /
						  256U);
				const uint8_t dim = (uint8_t)((uint16_t)luma * 35U / 100U);
				dst[idx + 0U] = dim;
				dst[idx + 1U] = dim;
				dst[idx + 2U] = dim;
				dst[idx + 3U] = 92U;
			}
		}
	}

	DrawHueRangeBar(out_debug_preview->hue_preview_bgra, width, height, config);
	out_debug_preview->hue_selected_pixels = selected_pixels;
	out_debug_preview->hue_selected_coverage = (float)selected_pixels / (float)pixel_count;
	out_debug_preview->has_hue_preview = true;
}

bool OpenCvRegionMaskPipeline::BuildComponentLut(const cv::Mat &stats,
						const OpenCvRegionParams &params,
						std::vector<uint8_t> &out_keep) const
{
	if (stats.empty() || stats.cols <= cv::CC_STAT_AREA)
		return false;

	const int component_count = stats.rows;
	if (component_count <= 0)
		return false;

	const int min_area =
		(int)std::lround(Clamp(params.min_area_px, 0.0f, 1024.0f * 1024.0f));
	const int min_side = (int)std::lround(Clamp(params.min_side_px, 0.0f, 4096.0f));
	const float min_fill = Clamp(params.min_fill, 0.0f, 1.0f);

	out_keep.assign((size_t)component_count, 0);
	for (int label = 1; label < component_count; ++label) {
		const int area = stats.at<int>(label, cv::CC_STAT_AREA);
		const int bbox_width = stats.at<int>(label, cv::CC_STAT_WIDTH);
		const int bbox_height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
		if (area <= 0 || bbox_width <= 0 || bbox_height <= 0)
			continue;

		const int bbox_area = bbox_width * bbox_height;
		if (bbox_area <= 0)
			continue;

		const float fill_ratio = (float)area / (float)bbox_area;
		if (area < min_area)
			continue;
		if (bbox_width < min_side || bbox_height < min_side)
			continue;
		if (fill_ratio < min_fill)
			continue;
		out_keep[(size_t)label] = 1;
	}

	return true;
}

bool OpenCvRegionMaskPipeline::BuildMask(const uint8_t *bgra, uint32_t width, uint32_t height,
					 uint32_t linesize,
					 const OpenCvRegionParams &params,
					 std::vector<uint8_t> &out_mask,
					 OpenCvRegionStats &out_stats,
					 OpenCvRegionDebugPreview *out_debug_preview,
					 std::vector<uint8_t> *out_expansion_gate,
					 OpenCvInputLayout input_layout) noexcept
{
	out_stats = {};
	if (out_debug_preview) {
		out_debug_preview->has_hue_preview = false;
		out_debug_preview->hue_preview_bgra.clear();
		out_debug_preview->hue_selected_pixels = 0;
		out_debug_preview->hue_selected_coverage = 0.0f;
	}
	const bool input_is_luma = input_layout == OpenCvInputLayout::Luma8;
	const uint32_t min_linesize = input_is_luma ? width : (width * 4U);
	if (!bgra || width == 0 || height == 0 || linesize < min_linesize)
		return false;

	const size_t pixel_count = (size_t)width * (size_t)height;
	if (pixel_count == 0)
		return false;

	try {
		cv::Mat bgra_view;
		if (input_is_luma) {
			grayscale_ = cv::Mat((int)height, (int)width, CV_8UC1,
					     const_cast<uint8_t *>(bgra), (size_t)linesize);
		} else {
			bgra_view = cv::Mat((int)height, (int)width, CV_8UC4, const_cast<uint8_t *>(bgra),
					    (size_t)linesize);
			cv::cvtColor(bgra_view, grayscale_, cv::COLOR_BGRA2GRAY);
		}
		cv::GaussianBlur(grayscale_, blurred_, cv::Size(5, 5), 0.0, 0.0,
				 cv::BORDER_REPLICATE);

		const double otsu_threshold = cv::threshold(
			blurred_, seed_mask_, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
		const float user_threshold_u8 =
			Clamp(params.threshold, 0.0f, 1.0f) * 255.0f;
		if (out_expansion_gate) {
			cv::threshold(grayscale_, expansion_gate_mask_,
				      (double)Clamp(user_threshold_u8, 0.0f, 255.0f), 255.0,
				      cv::THRESH_BINARY);
		}
		/*
		 * Keep threshold adaptation internal to stabilize region topology
		 * independently from the user-facing edge feather slider.
		 */
		const float adaptive_mix = Clamp(params.topology_softness, 0.0f, 1.0f);
		const float fused_threshold =
			(1.0f - adaptive_mix) * user_threshold_u8 + adaptive_mix * (float)otsu_threshold;
		cv::threshold(blurred_, seed_mask_, (double)Clamp(fused_threshold, 0.0f, 255.0f),
			      255.0, cv::THRESH_BINARY);

		bool has_explicit_color_gate = false;

		float luma_min = Clamp(params.luma_min, 0.0f, 1.0f);
		float luma_max = Clamp(params.luma_max, 0.0f, 1.0f);
		if (luma_min > luma_max)
			std::swap(luma_min, luma_max);
		const int luma_min_u8 = (int)std::lround(luma_min * 255.0f);
		const int luma_max_u8 = (int)std::lround(luma_max * 255.0f);
		const bool apply_luma_gate = luma_min_u8 > 0 || luma_max_u8 < 255;
		if (apply_luma_gate) {
			has_explicit_color_gate = true;
			cv::inRange(blurred_, cv::Scalar(luma_min_u8), cv::Scalar(luma_max_u8), luma_gate_);
			cv::bitwise_and(seed_mask_, luma_gate_, seed_mask_);
			if (out_expansion_gate) {
				cv::inRange(grayscale_, cv::Scalar(luma_min_u8), cv::Scalar(luma_max_u8),
					    strict_luma_gate_);
				cv::bitwise_and(expansion_gate_mask_, strict_luma_gate_,
						expansion_gate_mask_);
			}
		}

		float saturation_min = Clamp(params.saturation_min, 0.0f, 1.0f);
		float saturation_max = Clamp(params.saturation_max, 0.0f, 1.0f);
		if (saturation_min > saturation_max)
			std::swap(saturation_min, saturation_max);
		const int sat_min_u8 = (int)std::lround(saturation_min * 255.0f);
		const int sat_max_u8 = (int)std::lround(saturation_max * 255.0f);
		const bool apply_saturation_gate = sat_min_u8 > 0 || sat_max_u8 < 255;
		HueQualifier hue_qualifier;
		hue_qualifier.Configure(params.hue_qualifier);
		const bool apply_hue_qualifier_gate = hue_qualifier.HasActiveBands();
		const bool apply_legacy_hue_gate =
			!apply_hue_qualifier_gate &&
			!IsFullHueRange(params.hue_min_degrees, params.hue_max_degrees);
		if (input_is_luma &&
		    (apply_saturation_gate || apply_hue_qualifier_gate || apply_legacy_hue_gate ||
		     (out_debug_preview && params.capture_hue_debug_preview))) {
			return false;
		}

		if (apply_saturation_gate || apply_hue_qualifier_gate || apply_legacy_hue_gate) {
			cv::cvtColor(bgra_view, bgr_, cv::COLOR_BGRA2BGR);
			cv::cvtColor(bgr_, hsv_, cv::COLOR_BGR2HSV);
		}

		if (apply_saturation_gate) {
			has_explicit_color_gate = true;
			cv::extractChannel(hsv_, saturation_channel_, 1);
			cv::inRange(saturation_channel_, cv::Scalar(sat_min_u8), cv::Scalar(sat_max_u8),
				    saturation_gate_);
			cv::bitwise_and(seed_mask_, saturation_gate_, seed_mask_);
			if (out_expansion_gate)
				cv::bitwise_and(expansion_gate_mask_, saturation_gate_, expansion_gate_mask_);
		}

		if (apply_hue_qualifier_gate) {
			has_explicit_color_gate = true;
			cv::extractChannel(hsv_, hue_channel_, 0);
			const cv::Mat selection_lut(1, 256, CV_8UC1,
						    const_cast<uint8_t *>(hue_qualifier.SelectionLookup().data()));
			const cv::Mat applied_lut(1, 256, CV_8UC1,
						  const_cast<uint8_t *>(hue_qualifier.AppliedLookup().data()));
			cv::LUT(hue_channel_, applied_lut, hue_gate_);
			cv::bitwise_and(seed_mask_, hue_gate_, seed_mask_);
			if (out_expansion_gate)
				cv::bitwise_and(expansion_gate_mask_, hue_gate_, expansion_gate_mask_);

			if (out_debug_preview && params.capture_hue_debug_preview) {
				cv::LUT(hue_channel_, selection_lut, hue_selection_gate_);
				BuildHuePreview(bgra_view, hue_selection_gate_, params.hue_qualifier,
						out_debug_preview);
			}
		} else if (apply_legacy_hue_gate) {
			has_explicit_color_gate = true;
			cv::extractChannel(hsv_, hue_channel_, 0);
			const int hue_min = HueDegreesToOpenCv(params.hue_min_degrees);
			const int hue_max = HueDegreesToOpenCv(params.hue_max_degrees);
			if (hue_min <= hue_max) {
				cv::inRange(hue_channel_, cv::Scalar(hue_min), cv::Scalar(hue_max),
					    hue_gate_legacy_);
			} else {
				cv::inRange(hue_channel_, cv::Scalar(0), cv::Scalar(hue_max), hue_gate_low_);
				cv::inRange(hue_channel_, cv::Scalar(hue_min), cv::Scalar(179), hue_gate_high_);
				cv::bitwise_or(hue_gate_low_, hue_gate_high_, hue_gate_legacy_);
			}
			cv::bitwise_and(seed_mask_, hue_gate_legacy_, seed_mask_);
			if (out_expansion_gate)
				cv::bitwise_and(expansion_gate_mask_, hue_gate_legacy_, expansion_gate_mask_);
		}

		const int close_kernel_size =
			OddKernelFromSoftness(params.topology_softness, 3, 31);
		const int open_kernel_size =
			std::max(3, (close_kernel_size / 3) | 1);
		const cv::Mat close_kernel = cv::getStructuringElement(
			cv::MORPH_ELLIPSE, cv::Size(close_kernel_size, close_kernel_size));
		const cv::Mat open_kernel = cv::getStructuringElement(
			cv::MORPH_ELLIPSE, cv::Size(open_kernel_size, open_kernel_size));
		cv::morphologyEx(seed_mask_, closed_mask_, cv::MORPH_CLOSE, close_kernel,
				 cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
		cv::morphologyEx(closed_mask_, opened_mask_, cv::MORPH_OPEN, open_kernel,
				 cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
		if (out_expansion_gate) {
			const size_t row_bytes = (size_t)width;
			out_expansion_gate->resize(pixel_count);
			if (expansion_gate_mask_.isContinuous()) {
				memcpy(out_expansion_gate->data(), expansion_gate_mask_.ptr<uint8_t>(),
				       pixel_count);
			} else {
				for (uint32_t y = 0; y < height; ++y) {
					const uint8_t *src_row = expansion_gate_mask_.ptr<uint8_t>((int)y);
					uint8_t *dst_row = out_expansion_gate->data() + (size_t)y * row_bytes;
					memcpy(dst_row, src_row, row_bytes);
				}
			}
		}

		const int component_count =
			BuildConnectedComponentsWithStats(opened_mask_, labels_, stats_, centroids_);
		std::vector<uint8_t> keep_components;
		if (!BuildComponentLut(stats_, params, keep_components))
			return false;

		selected_mask_.create((int)height, (int)width, CV_8UC1);
		selected_mask_.setTo(cv::Scalar(0));

		uint32_t kept_pixels = 0;
		uint32_t kept_components = 0;
		for (int label = 1; label < component_count; ++label) {
			if (label < 0 || (size_t)label >= keep_components.size())
				continue;
			if (keep_components[(size_t)label] == 0)
				continue;
			kept_components++;
		}

		for (uint32_t y = 0; y < height; ++y) {
			const int *label_row = labels_.ptr<int>((int)y);
			uint8_t *mask_row = selected_mask_.ptr<uint8_t>((int)y);
			for (uint32_t x = 0; x < width; ++x) {
				const int label = label_row[x];
				if (label <= 0)
					continue;
				if ((size_t)label >= keep_components.size())
					continue;
				if (keep_components[(size_t)label] == 0)
					continue;
				mask_row[x] = 255;
				kept_pixels++;
			}
		}

		float kept_coverage = (float)kept_pixels / (float)pixel_count;
		const float min_coverage = Clamp(params.min_coverage, 0.0f, 1.0f);
		if (kept_pixels == 0 || kept_components == 0 || kept_coverage < min_coverage) {
			const uint32_t opened_pixels = (uint32_t)cv::countNonZero(opened_mask_);
			const float opened_coverage = (float)opened_pixels / (float)pixel_count;
			/*
			 * Fail-open only when the candidate foreground is clearly a large
			 * coherent region. This avoids reverting to text-only inversion
			 * while still protecting against over-strict component thresholds.
			 */
			const float fallback_min_coverage = std::max(min_coverage * 4.0f, 0.02f);
			if (!has_explicit_color_gate && opened_pixels > 0 &&
			    opened_coverage >= fallback_min_coverage) {
				opened_mask_.copyTo(selected_mask_);
				kept_pixels = opened_pixels;
				kept_components = component_count > 1 ? (uint32_t)(component_count - 1) : 0;
				kept_coverage = opened_coverage;
			}
		}

		const int soften_kernel_size =
			OddKernelFromSoftness(params.edge_softness, 1, 21);
		if (soften_kernel_size > 1) {
			cv::GaussianBlur(selected_mask_, softened_mask_,
					 cv::Size(soften_kernel_size, soften_kernel_size),
					 0.0, 0.0, cv::BORDER_REPLICATE);
		} else {
			selected_mask_.copyTo(softened_mask_);
		}

		const float coverage_floor = Clamp(params.coverage, 0.0f, 0.98f);
		if (coverage_floor > 0.0001f) {
			softened_mask_.convertTo(coverage_float_, CV_32F, 1.0 / 255.0);
			coverage_float_ =
				(coverage_float_ - coverage_floor) /
				std::max(0.0001f, 1.0f - coverage_floor);
			cv::max(coverage_float_, 0.0f, coverage_float_);
			cv::min(coverage_float_, 1.0f, coverage_float_);
			coverage_float_.convertTo(softened_mask_, CV_8U, 255.0);
		}

		if (!softened_mask_.isContinuous())
			softened_mask_ = softened_mask_.clone();

		out_mask.resize(pixel_count);
		if (softened_mask_.isContinuous()) {
			memcpy(out_mask.data(), softened_mask_.ptr<uint8_t>(), pixel_count);
		} else {
			const size_t row_bytes = (size_t)width;
			for (uint32_t y = 0; y < height; ++y) {
				const uint8_t *src_row = softened_mask_.ptr<uint8_t>((int)y);
				uint8_t *dst_row = out_mask.data() + (size_t)y * row_bytes;
				memcpy(dst_row, src_row, row_bytes);
			}
		}

		out_stats.accepted_pixels = (uint32_t)cv::countNonZero(softened_mask_);
		out_stats.accepted_components = kept_components;
		out_stats.accepted_coverage =
			(float)out_stats.accepted_pixels / (float)pixel_count;
		return true;
	} catch (...) {
		out_mask.clear();
		out_stats = {};
		return false;
	}
}

} // namespace lenses::filter::invert
