#include "runtime/deterministic/analysis.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace lenses_autograde::deterministic {
namespace {

constexpr float kShadesOfGrayP = 6.0f;
constexpr float kGrayEdgeP = 6.0f;
constexpr size_t kObjectiveSampleTarget = 4096;

bool ReadUnpremultipliedRgb(const uint8_t *bgra, Rgb &out_raw)
{
	const float alpha = (float)bgra[3] * (1.0f / 255.0f);
	if (alpha <= 0.001f)
		return false;

	const float inv_alpha = 1.0f / alpha;
	out_raw.r = Clamp01((float)bgra[2] * (1.0f / 255.0f) * inv_alpha);
	out_raw.g = Clamp01((float)bgra[1] * (1.0f / 255.0f) * inv_alpha);
	out_raw.b = Clamp01((float)bgra[0] * (1.0f / 255.0f) * inv_alpha);
	return true;
}

void DecodeInputRgb(const Rgb &raw, InputTransfer transfer, Rgb &out_nl, Rgb &out_lin)
{
	switch (transfer) {
	case InputTransfer::SrgbNonlinear:
		out_nl = raw;
		out_lin.r = SrgbToLinear(raw.r);
		out_lin.g = SrgbToLinear(raw.g);
		out_lin.b = SrgbToLinear(raw.b);
		break;
	case InputTransfer::Linear:
		out_lin = raw;
		out_nl.r = LinearToSrgb(raw.r);
		out_nl.g = LinearToSrgb(raw.g);
		out_nl.b = LinearToSrgb(raw.b);
		break;
	}
}

} // namespace

const char *TransferLabel(InputTransfer transfer)
{
	switch (transfer) {
	case InputTransfer::SrgbNonlinear:
		return "srgb_nl";
	case InputTransfer::Linear:
		return "linear";
	}
	return "unknown";
}

float HistogramPercentile(const std::array<uint64_t, kLumaHistogramBins> &hist, uint64_t total,
			  float percentile)
{
	if (total == 0)
		return 0.0f;
	const float p = Clamp01(percentile);
	const uint64_t threshold = (uint64_t)std::ceil((double)total * (double)p);
	uint64_t acc = 0;
	for (size_t i = 0; i < hist.size(); ++i) {
		acc += hist[i];
		if (acc >= threshold)
			return (float)i * (1.0f / (float)(kLumaHistogramBins - 1U));
	}
	return 1.0f;
}

bool AnalyzeRoiFromBgra(const uint8_t *bgra, uint32_t width, uint32_t height, uint32_t linesize,
			InputTransfer transfer, RoiAnalysis &out_analysis, std::string &out_detail)
{
	out_analysis = {};
	out_detail.clear();

	if (!bgra || width == 0 || height == 0 || linesize < width * 4U) {
		out_detail = "analysis: invalid frame input";
		return false;
	}

	const uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
	const double max_samples = 180000.0;
	const uint32_t step =
		pixel_count > (uint64_t)max_samples
			? std::max<uint32_t>(
				  1U,
				  (uint32_t)std::ceil(std::sqrt((double)pixel_count / max_samples)))
			: 1U;

	const double max_objective_samples = (double)kObjectiveSampleTarget;
	const uint32_t objective_step =
		pixel_count > (uint64_t)max_objective_samples
			? std::max<uint32_t>(
				  1U,
				  (uint32_t)std::ceil(std::sqrt((double)pixel_count / max_objective_samples)))
			: 1U;
	out_analysis.objective_samples.reserve(kObjectiveSampleTarget);

	for (uint32_t y = 0; y < height; y += step) {
		const uint8_t *row = bgra + (size_t)y * linesize;
		for (uint32_t x = 0; x < width; x += step) {
			const uint8_t *p = row + (size_t)x * 4U;
			Rgb raw{};
			if (!ReadUnpremultipliedRgb(p, raw))
				continue;

			Rgb nl{};
			Rgb lin{};
			DecodeInputRgb(raw, transfer, nl, lin);

			const float r_nl = nl.r;
			const float g_nl = nl.g;
			const float b_nl = nl.b;
			const float r_lin = lin.r;
			const float g_lin = lin.g;
			const float b_lin = lin.b;

			const float luma_nl = Clamp01(LumaNonlinear(r_nl, g_nl, b_nl));
			const float luma_lin = std::max(LumaLinear(r_lin, g_lin, b_lin), 1e-4f);
			const float sat_nl = SaturationFromRgb(r_nl, g_nl, b_nl);

			const uint32_t bin = (uint32_t)Clamp(
				std::round(luma_nl * (float)(kLumaHistogramBins - 1U)), 0.0f,
				(float)(kLumaHistogramBins - 1U));

			out_analysis.luma_hist[bin] += 1U;
			out_analysis.sample_count += 1U;
			out_analysis.sum_r_nl += r_nl;
			out_analysis.sum_g_nl += g_nl;
			out_analysis.sum_b_nl += b_nl;
			out_analysis.sum_r_lin += r_lin;
			out_analysis.sum_g_lin += g_lin;
			out_analysis.sum_b_lin += b_lin;
			out_analysis.sum_luma_nl += luma_nl;
			out_analysis.sum_sat_nl += sat_nl;
			out_analysis.sum_log_luma_lin += std::log(luma_lin);
			out_analysis.shadow_pixels += luma_nl < 0.02f ? 1U : 0U;
			out_analysis.highlight_pixels += luma_nl > 0.98f ? 1U : 0U;

			float neutral_weight = Clamp01(1.0f - sat_nl * 1.8f);
			const float color_delta =
				(std::fabs(r_nl - g_nl) + std::fabs(g_nl - b_nl) + std::fabs(b_nl - r_nl)) *
				0.5f;
			neutral_weight *= Clamp01(1.0f - color_delta * 1.5f);
			const float midtone_weight = SmoothStep(0.05f, 0.22f, luma_nl) *
						     (1.0f - SmoothStep(0.75f, 0.97f, luma_nl));
			const float wb_weight = neutral_weight * midtone_weight;
			out_analysis.wb_weight_sum += wb_weight;
			out_analysis.wb_r_lin_sum += wb_weight * r_lin;
			out_analysis.wb_g_lin_sum += wb_weight * g_lin;
			out_analysis.wb_b_lin_sum += wb_weight * b_lin;

			if (sat_nl < 0.98f && luma_nl > 0.01f) {
				out_analysis.wb_pow_r_sum += std::pow(std::max(r_lin, 1e-5f), kShadesOfGrayP);
				out_analysis.wb_pow_g_sum += std::pow(std::max(g_lin, 1e-5f), kShadesOfGrayP);
				out_analysis.wb_pow_b_sum += std::pow(std::max(b_lin, 1e-5f), kShadesOfGrayP);
				out_analysis.wb_pow_weight_sum += 1.0;
			}

			Rgb grad_raw_x{};
			Rgb grad_raw_y{};
			bool has_grad_x = false;
			bool has_grad_y = false;
			if (x + step < width) {
				const uint8_t *p_right = row + (size_t)(x + step) * 4U;
				has_grad_x = ReadUnpremultipliedRgb(p_right, grad_raw_x);
			}
			if (y + step < height) {
				const uint8_t *row_next = bgra + (size_t)(y + step) * linesize;
				const uint8_t *p_down = row_next + (size_t)x * 4U;
				has_grad_y = ReadUnpremultipliedRgb(p_down, grad_raw_y);
			}
			if (has_grad_x || has_grad_y) {
				float dr = 0.0f;
				float dg = 0.0f;
				float db = 0.0f;
				float grad_luma = 0.0f;
				float grad_count = 0.0f;
				if (has_grad_x) {
					Rgb right_nl{};
					Rgb right_lin{};
					DecodeInputRgb(grad_raw_x, transfer, right_nl, right_lin);
					(void)right_nl;
					dr += std::fabs(r_lin - right_lin.r);
					dg += std::fabs(g_lin - right_lin.g);
					db += std::fabs(b_lin - right_lin.b);
					grad_luma +=
						std::fabs(luma_lin - LumaLinear(right_lin.r, right_lin.g, right_lin.b));
					grad_count += 1.0f;
				}
				if (has_grad_y) {
					Rgb down_nl{};
					Rgb down_lin{};
					DecodeInputRgb(grad_raw_y, transfer, down_nl, down_lin);
					(void)down_nl;
					dr += std::fabs(r_lin - down_lin.r);
					dg += std::fabs(g_lin - down_lin.g);
					db += std::fabs(b_lin - down_lin.b);
					grad_luma +=
						std::fabs(luma_lin - LumaLinear(down_lin.r, down_lin.g, down_lin.b));
					grad_count += 1.0f;
				}

				const float inv_count = grad_count > 0.0f ? (1.0f / grad_count) : 1.0f;
				dr = std::max(dr * inv_count, 1e-5f);
				dg = std::max(dg * inv_count, 1e-5f);
				db = std::max(db * inv_count, 1e-5f);
				out_analysis.wb_edge_r_sum += std::pow(dr, kGrayEdgeP);
				out_analysis.wb_edge_g_sum += std::pow(dg, kGrayEdgeP);
				out_analysis.wb_edge_b_sum += std::pow(db, kGrayEdgeP);
				out_analysis.wb_edge_weight_sum += 1.0;

				const float grad_luma_mean = grad_luma * inv_count;
				out_analysis.luma_grad_sum += grad_luma_mean;
				out_analysis.luma_grad_sq_sum += grad_luma_mean * grad_luma_mean;
				out_analysis.luma_grad_count += 1.0;
			}

			if ((x % objective_step == 0) && (y % objective_step == 0) &&
			    out_analysis.objective_samples.size() < kObjectiveSampleTarget) {
				PixelSample sample{};
				sample.nl = nl;
				sample.lin = lin;
				sample.luma_nl = luma_nl;
				sample.sat_nl = sat_nl;
				out_analysis.objective_samples.push_back(sample);
			}
		}
	}

	if (out_analysis.sample_count < 64U) {
		out_detail = "analysis: insufficient valid samples";
		return false;
	}

	char detail[256] = {};
	(void)snprintf(detail, sizeof(detail),
		       "analysis transfer=%s samples=%" PRIu64 " objective=%zu step=%u",
		       TransferLabel(transfer), out_analysis.sample_count,
		       out_analysis.objective_samples.size(), step);
	out_detail = detail;
	return true;
}

} // namespace lenses_autograde::deterministic
