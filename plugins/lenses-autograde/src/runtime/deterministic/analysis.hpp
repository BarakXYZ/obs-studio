#pragma once

#include "runtime/deterministic/color-math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lenses_autograde::deterministic {

constexpr size_t kLumaHistogramBins = 1024;

enum class InputTransfer : uint32_t {
	SrgbNonlinear = 0,
	Linear = 1,
};

struct PixelSample {
	Rgb nl{};
	Rgb lin{};
	float luma_nl = 0.0f;
	float sat_nl = 0.0f;
};

struct RoiAnalysis {
	uint64_t sample_count = 0;

	double sum_r_nl = 0.0;
	double sum_g_nl = 0.0;
	double sum_b_nl = 0.0;
	double sum_r_lin = 0.0;
	double sum_g_lin = 0.0;
	double sum_b_lin = 0.0;
	double sum_luma_nl = 0.0;
	double sum_sat_nl = 0.0;
	double sum_log_luma_lin = 0.0;

	double wb_weight_sum = 0.0;
	double wb_r_lin_sum = 0.0;
	double wb_g_lin_sum = 0.0;
	double wb_b_lin_sum = 0.0;

	double wb_pow_r_sum = 0.0;
	double wb_pow_g_sum = 0.0;
	double wb_pow_b_sum = 0.0;
	double wb_pow_weight_sum = 0.0;

	double wb_edge_r_sum = 0.0;
	double wb_edge_g_sum = 0.0;
	double wb_edge_b_sum = 0.0;
	double wb_edge_weight_sum = 0.0;

	double luma_grad_sum = 0.0;
	double luma_grad_sq_sum = 0.0;
	double luma_grad_count = 0.0;

	uint64_t shadow_pixels = 0;
	uint64_t highlight_pixels = 0;
	std::array<uint64_t, kLumaHistogramBins> luma_hist{};
	std::vector<PixelSample> objective_samples{};
};

bool AnalyzeRoiFromBgra(const uint8_t *bgra, uint32_t width, uint32_t height, uint32_t linesize,
			InputTransfer transfer, RoiAnalysis &out_analysis, std::string &out_detail);

float HistogramPercentile(const std::array<uint64_t, kLumaHistogramBins> &hist, uint64_t total,
			  float percentile);

const char *TransferLabel(InputTransfer transfer);

} // namespace lenses_autograde::deterministic
