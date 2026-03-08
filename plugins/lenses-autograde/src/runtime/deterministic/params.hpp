#pragma once

#include "runtime/deterministic/analysis.hpp"

#include <string>

namespace lenses_autograde::deterministic {

struct GradeParams {
	float wb_r = 1.0f;
	float wb_g = 1.0f;
	float wb_b = 1.0f;
	float exposure = 1.0f;
	float contrast = 1.0f;
	float saturation = 1.0f;
	float vibrance = 1.0f;
	float shadow_lift = 0.0f;
	float shoulder_strength = 0.5f;
	float filmic_white = 1.7f;
	float pivot_nl = 0.45f;
	float confidence = 0.0f;
	float objective_score = -1e9f;
	float detail_amount = 0.0f;

	float p01 = 0.0f;
	float p05 = 0.0f;
	float p50 = 0.0f;
	float p95 = 1.0f;
	float p99 = 1.0f;
	float spread = 0.0f;
	float mean_sat_nl = 0.2f;
	float mean_luma_nl = 0.5f;
	float log_avg_luma_lin = 0.18f;
	float shadow_ratio = 0.0f;
	float highlight_ratio = 0.0f;
	float wb_ratio = 0.0f;
	float edge_conf = 0.0f;
	float grad_mean = 0.0f;
	float grad_std = 0.0f;
};

bool SolveGradeParams(const RoiAnalysis &analysis, GradeParams &out_params, std::string &out_detail);

Rgb ApplyTransformToLinear(const Rgb &input_nl, const GradeParams &params);

float ScoreParamsOnSamples(const RoiAnalysis &analysis, const GradeParams &params);

} // namespace lenses_autograde::deterministic
