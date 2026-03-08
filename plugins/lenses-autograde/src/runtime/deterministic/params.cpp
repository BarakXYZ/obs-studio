#include "runtime/deterministic/params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace lenses_autograde::deterministic {
namespace {

struct LumaDistribution {
	float p01 = 0.0f;
	float p05 = 0.0f;
	float p50 = 0.0f;
	float p95 = 1.0f;
	float p99 = 1.0f;
	float spread = 0.0f;
	float entropy = 0.0f;
	float shadow_clip = 0.0f;
	float highlight_clip = 0.0f;
};

float HueDegrees(float r, float g, float b)
{
	const float max_c = std::max(r, std::max(g, b));
	const float min_c = std::min(r, std::min(g, b));
	const float delta = max_c - min_c;
	if (delta <= kEpsilon)
		return 0.0f;

	float h = 0.0f;
	if (max_c == r) {
		h = (g - b) / delta;
		if (g < b)
			h += 6.0f;
	} else if (max_c == g) {
		h = ((b - r) / delta) + 2.0f;
	} else {
		h = ((r - g) / delta) + 4.0f;
	}
	return h * 60.0f;
}

float SkinProtectionMask(const Rgb &nl)
{
	const float sat = SaturationFromRgb(nl.r, nl.g, nl.b);
	if (sat < 0.08f || sat > 0.92f)
		return 0.0f;

	const float lum = LumaNonlinear(nl.r, nl.g, nl.b);
	if (lum < 0.07f || lum > 0.98f)
		return 0.0f;

	const float hue = HueDegrees(nl.r, nl.g, nl.b);
	const float center = 32.0f;
	const float dist = std::min(std::fabs(hue - center), 360.0f - std::fabs(hue - center));
	const float hue_score = Clamp01(1.0f - dist / 30.0f);
	const float sat_score =
		SmoothStep(0.10f, 0.28f, sat) * (1.0f - SmoothStep(0.68f, 0.92f, sat));
	const float lum_score =
		SmoothStep(0.12f, 0.25f, lum) * (1.0f - SmoothStep(0.75f, 0.96f, lum));
	return hue_score * sat_score * lum_score;
}

float AcesFitted(float x)
{
	x = std::max(0.0f, x);
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return Clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

float MapLuma(float y_lin, const GradeParams &p)
{
	float y = std::max(0.0f, y_lin * p.exposure);
	const float pivot = std::max(SrgbToLinear(p.pivot_nl), 1e-4f);
	const float pivot_log2 = std::log2(pivot + kEpsilon);
	const float log_y = std::log2(y + kEpsilon);
	const float mapped_log = (log_y - pivot_log2) * p.contrast + pivot_log2;
	y = std::exp2(mapped_log);

	if (p.shadow_lift > kEpsilon) {
		const float lift = p.shadow_lift * std::exp(-6.0f * y);
		y += lift;
	}

	const float white = std::max(p.filmic_white, 1.0f);
	const float reinhard = y / (1.0f + y);
	const float aces = AcesFitted((y / white) * 1.65f);
	return Lerp(reinhard, aces, Clamp01(p.shoulder_strength));
}

LumaDistribution ComputeDistribution(const std::vector<float> &values)
{
	LumaDistribution out{};
	if (values.empty())
		return out;

	std::array<uint32_t, 256> hist{};
	for (float v : values) {
		const int bin = (int)Clamp(std::round(v * 255.0f), 0.0f, 255.0f);
		hist[(size_t)bin] += 1U;
	}

	const auto percentile = [&](float p) {
		const uint64_t total = values.size();
		const uint64_t threshold = (uint64_t)std::ceil((double)total * (double)Clamp01(p));
		uint64_t acc = 0;
		for (size_t i = 0; i < hist.size(); ++i) {
			acc += hist[i];
			if (acc >= threshold)
				return (float)i * (1.0f / 255.0f);
		}
		return 1.0f;
	};

	out.p01 = percentile(0.01f);
	out.p05 = percentile(0.05f);
	out.p50 = percentile(0.50f);
	out.p95 = percentile(0.95f);
	out.p99 = percentile(0.99f);
	out.spread = std::max(0.0005f, out.p95 - out.p05);

	const float inv_total = 1.0f / (float)values.size();
	for (uint32_t c : hist) {
		if (c == 0)
			continue;
		const float p = (float)c * inv_total;
		out.entropy -= p * std::log2(std::max(p, 1e-8f));
	}

	uint32_t low = 0;
	uint32_t high = 0;
	for (float v : values) {
		low += v <= 0.01f ? 1U : 0U;
		high += v >= 0.99f ? 1U : 0U;
	}
	out.shadow_clip = (float)low * inv_total;
	out.highlight_clip = (float)high * inv_total;
	return out;
}

float EvaluateObjective(const RoiAnalysis &analysis, const GradeParams &params,
			float *out_mean_sat = nullptr)
{
	if (analysis.objective_samples.empty())
		return -1e9f;

	std::vector<float> luma_values;
	luma_values.reserve(analysis.objective_samples.size());
	double sat_sum = 0.0;

	for (const PixelSample &sample : analysis.objective_samples) {
		const Rgb out_lin = ApplyTransformToLinear(sample.nl, params);
		Rgb out_nl = {
			.r = LinearToSrgb(out_lin.r),
			.g = LinearToSrgb(out_lin.g),
			.b = LinearToSrgb(out_lin.b),
		};
		const float luma_nl = Clamp01(LumaNonlinear(out_nl.r, out_nl.g, out_nl.b));
		luma_values.push_back(luma_nl);
		sat_sum += SaturationFromRgb(out_nl.r, out_nl.g, out_nl.b);
	}

	const LumaDistribution dist = ComputeDistribution(luma_values);
	const float sat_mean =
		(float)(sat_sum / std::max<size_t>(1U, analysis.objective_samples.size()));
	if (out_mean_sat)
		*out_mean_sat = sat_mean;

	const float target_mid = 0.46f;
	const float target_spread = 0.57f;
	const float target_entropy = 6.90f;

	const float mid_penalty = std::fabs(dist.p50 - target_mid);
	const float spread_penalty = std::fabs(dist.spread - target_spread);
	const float clip_penalty = 1.4f * dist.highlight_clip + 1.1f * dist.shadow_clip;
	const float sat_penalty = std::max(0.0f, sat_mean - 0.58f) * 0.85f;
	const float entropy_bonus = Clamp(dist.entropy - target_entropy, -1.2f, 1.2f) * 0.35f;

	return 1.0f + entropy_bonus - 2.0f * mid_penalty - 1.35f * spread_penalty - clip_penalty -
	       sat_penalty;
}

} // namespace

Rgb ApplyTransformToLinear(const Rgb &input_nl, const GradeParams &p)
{
	Rgb lin = {
		.r = SrgbToLinear(input_nl.r) * p.wb_r,
		.g = SrgbToLinear(input_nl.g) * p.wb_g,
		.b = SrgbToLinear(input_nl.b) * p.wb_b,
	};

	const float y_lin = LumaLinear(lin.r, lin.g, lin.b);
	const float y_mapped = MapLuma(y_lin, p);
	if (y_lin > kEpsilon) {
		const float scale = y_mapped / y_lin;
		lin.r *= scale;
		lin.g *= scale;
		lin.b *= scale;
	}

	lin.r = std::max(0.0f, lin.r);
	lin.g = std::max(0.0f, lin.g);
	lin.b = std::max(0.0f, lin.b);

	Rgb out_nl = {
		.r = LinearToSrgb(lin.r),
		.g = LinearToSrgb(lin.g),
		.b = LinearToSrgb(lin.b),
	};

	const float luma_nl = Clamp01(LumaNonlinear(out_nl.r, out_nl.g, out_nl.b));
	const float sat_now = SaturationFromRgb(out_nl.r, out_nl.g, out_nl.b);
	const float skin_mask = SkinProtectionMask(out_nl);

	float sat_scale = Lerp(p.saturation, 1.0f + (p.saturation - 1.0f) * 0.45f, skin_mask);
	sat_scale = Lerp(sat_scale, 1.0f, SmoothStep(0.78f, 1.0f, luma_nl) * 0.35f);
	float vib = 1.0f + (1.0f - sat_now) * (p.vibrance - 1.0f);
	vib = Lerp(vib, 1.0f + (vib - 1.0f) * 0.55f, skin_mask);
	vib = Lerp(vib, 1.0f, SmoothStep(0.78f, 1.0f, luma_nl) * 0.25f);

	const float chroma_scale = Clamp(sat_scale * vib, 0.88f, 1.18f);
	Oklab lab = LinearSrgbToOklab(lin);
	lab.a *= chroma_scale;
	lab.b *= chroma_scale;

	Rgb out_lin = OklabToLinearSrgb(lab);
	out_lin.r = std::max(0.0f, out_lin.r);
	out_lin.g = std::max(0.0f, out_lin.g);
	out_lin.b = std::max(0.0f, out_lin.b);

	const float out_luma_lin = std::max(LumaLinear(out_lin.r, out_lin.g, out_lin.b), kEpsilon);
	const float luma_preserve = Clamp(y_mapped / out_luma_lin, 0.74f, 1.32f);
	out_lin.r = Clamp01(out_lin.r * luma_preserve);
	out_lin.g = Clamp01(out_lin.g * luma_preserve);
	out_lin.b = Clamp01(out_lin.b * luma_preserve);

	return out_lin;
}

float ScoreParamsOnSamples(const RoiAnalysis &analysis, const GradeParams &params)
{
	return EvaluateObjective(analysis, params, nullptr);
}

bool SolveGradeParams(const RoiAnalysis &analysis, GradeParams &out_params, std::string &out_detail)
{
	out_params = {};
	out_detail.clear();

	if (analysis.sample_count < 64U || analysis.objective_samples.size() < 128U) {
		out_detail = "solve: insufficient analysis samples";
		return false;
	}

	const float count = (float)analysis.sample_count;
	const float mean_r_lin = (float)(analysis.sum_r_lin / count);
	const float mean_g_lin = (float)(analysis.sum_g_lin / count);
	const float mean_b_lin = (float)(analysis.sum_b_lin / count);
	const float mean_luma_lin = LumaLinear(mean_r_lin, mean_g_lin, mean_b_lin);

	out_params.mean_luma_nl = (float)(analysis.sum_luma_nl / count);
	out_params.mean_sat_nl = (float)(analysis.sum_sat_nl / count);
	out_params.log_avg_luma_lin =
		std::exp((float)(analysis.sum_log_luma_lin / std::max(1.0f, count)));
	out_params.shadow_ratio = (float)analysis.shadow_pixels / count;
	out_params.highlight_ratio = (float)analysis.highlight_pixels / count;
	out_params.wb_ratio =
		(float)(analysis.wb_weight_sum / std::max<double>(1.0, (double)analysis.sample_count));
	out_params.p01 = HistogramPercentile(analysis.luma_hist, analysis.sample_count, 0.01f);
	out_params.p05 = HistogramPercentile(analysis.luma_hist, analysis.sample_count, 0.05f);
	out_params.p50 = HistogramPercentile(analysis.luma_hist, analysis.sample_count, 0.50f);
	out_params.p95 = HistogramPercentile(analysis.luma_hist, analysis.sample_count, 0.95f);
	out_params.p99 = HistogramPercentile(analysis.luma_hist, analysis.sample_count, 0.99f);
	out_params.spread = std::max(0.001f, out_params.p95 - out_params.p05);

	const float grad_count = analysis.luma_grad_count > 0.0 ? (float)analysis.luma_grad_count : 1.0f;
	out_params.grad_mean = (float)(analysis.luma_grad_sum / grad_count);
	const float grad_sq_mean = (float)(analysis.luma_grad_sq_sum / grad_count);
	out_params.grad_std =
		std::sqrt(std::max(0.0f, grad_sq_mean - out_params.grad_mean * out_params.grad_mean));

	const float neutral_den =
		analysis.wb_weight_sum > 1.0 ? (float)analysis.wb_weight_sum : count;
	const float wb_neutral_r = (float)(analysis.wb_weight_sum > 1.0
						   ? analysis.wb_r_lin_sum / neutral_den
						   : analysis.sum_r_lin / count);
	const float wb_neutral_g = (float)(analysis.wb_weight_sum > 1.0
						   ? analysis.wb_g_lin_sum / neutral_den
						   : analysis.sum_g_lin / count);
	const float wb_neutral_b = (float)(analysis.wb_weight_sum > 1.0
						   ? analysis.wb_b_lin_sum / neutral_den
						   : analysis.sum_b_lin / count);

	const float wb_pow_den =
		analysis.wb_pow_weight_sum > 1.0 ? (float)analysis.wb_pow_weight_sum : count;
	const float wb_pow_r = analysis.wb_pow_weight_sum > 1.0
				       ? std::pow((float)(analysis.wb_pow_r_sum / wb_pow_den), 1.0f / 6.0f)
				       : mean_r_lin;
	const float wb_pow_g = analysis.wb_pow_weight_sum > 1.0
				       ? std::pow((float)(analysis.wb_pow_g_sum / wb_pow_den), 1.0f / 6.0f)
				       : mean_g_lin;
	const float wb_pow_b = analysis.wb_pow_weight_sum > 1.0
				       ? std::pow((float)(analysis.wb_pow_b_sum / wb_pow_den), 1.0f / 6.0f)
				       : mean_b_lin;

	const float wb_edge_den =
		analysis.wb_edge_weight_sum > 1.0 ? (float)analysis.wb_edge_weight_sum : count;
	const float wb_edge_r = analysis.wb_edge_weight_sum > 1.0
					? std::pow((float)(analysis.wb_edge_r_sum / wb_edge_den), 1.0f / 6.0f)
					: wb_pow_r;
	const float wb_edge_g = analysis.wb_edge_weight_sum > 1.0
					? std::pow((float)(analysis.wb_edge_g_sum / wb_edge_den), 1.0f / 6.0f)
					: wb_pow_g;
	const float wb_edge_b = analysis.wb_edge_weight_sum > 1.0
					? std::pow((float)(analysis.wb_edge_b_sum / wb_edge_den), 1.0f / 6.0f)
					: wb_pow_b;

	const float wb_blend = Clamp01((out_params.wb_ratio - 0.02f) / 0.14f);
	const float wb_base_r = Lerp(wb_pow_r, wb_neutral_r, 0.45f + 0.35f * wb_blend);
	const float wb_base_g = Lerp(wb_pow_g, wb_neutral_g, 0.45f + 0.35f * wb_blend);
	const float wb_base_b = Lerp(wb_pow_b, wb_neutral_b, 0.45f + 0.35f * wb_blend);
	const float edge_conf = Clamp01((float)analysis.wb_edge_weight_sum / 1600.0f);
	const float wb_est_r = Lerp(wb_base_r, wb_edge_r, edge_conf * 0.30f);
	const float wb_est_g = Lerp(wb_base_g, wb_edge_g, edge_conf * 0.30f);
	const float wb_est_b = Lerp(wb_base_b, wb_edge_b, edge_conf * 0.30f);
	out_params.edge_conf = edge_conf;

	const float wb_target = (wb_est_r + wb_est_g + wb_est_b) * (1.0f / 3.0f);
	out_params.wb_r = Clamp(wb_target / std::max(wb_est_r, 0.01f), 0.88f, 1.12f);
	out_params.wb_g = Clamp(wb_target / std::max(wb_est_g, 0.01f), 0.88f, 1.12f);
	out_params.wb_b = Clamp(wb_target / std::max(wb_est_b, 0.01f), 0.88f, 1.12f);

	const float wb_luma_lin = LumaLinear(mean_r_lin * out_params.wb_r, mean_g_lin * out_params.wb_g,
					      mean_b_lin * out_params.wb_b);
	if (wb_luma_lin > kEpsilon) {
		const float wb_norm = Clamp(mean_luma_lin / wb_luma_lin, 0.96f, 1.04f);
		out_params.wb_r *= wb_norm;
		out_params.wb_g *= wb_norm;
		out_params.wb_b *= wb_norm;
	}

	const float flatness = Clamp01((0.18f - out_params.spread) / 0.18f);
	const float dynamic_conf = Clamp01((out_params.spread - 0.07f) / 0.42f);
	const float wb_conf = Clamp01((out_params.wb_ratio - 0.015f) / 0.20f);
	const float clip_penalty =
		Clamp01((out_params.highlight_ratio + out_params.shadow_ratio - 0.55f) / 0.40f);
	const float midtone_conf = 1.0f - Clamp01(std::fabs(out_params.p50 - 0.45f) / 0.45f);
	const float sample_conf = Clamp01((float)analysis.sample_count / 8000.0f);
	out_params.confidence = Clamp01(0.20f + 0.36f * dynamic_conf + 0.16f * wb_conf +
					0.16f * sample_conf + 0.12f * midtone_conf -
					0.20f * clip_penalty + flatness * 0.08f);

	out_params.contrast = Clamp(std::pow(0.56f / std::max(out_params.spread, 0.08f), 0.25f), 0.94f,
				    1.15f);
	out_params.contrast = Lerp(out_params.contrast, 1.06f, flatness * 0.30f);
	out_params.pivot_nl = Clamp(Lerp(Clamp(out_params.p50, 0.30f, 0.60f), 0.43f, flatness * 0.30f),
				    0.30f, 0.60f);
	out_params.shadow_lift = Clamp((0.10f - out_params.p05) * 0.10f + out_params.shadow_ratio * 0.025f,
				       0.0f, 0.045f);
	out_params.shoulder_strength =
		Clamp(0.34f + (out_params.p95 - 0.78f) * 0.80f + out_params.highlight_ratio * 0.30f,
		      0.28f, 0.66f);
	out_params.filmic_white =
		Clamp(1.30f + (out_params.p99 - 0.86f) * 1.30f, 1.15f, 1.98f);

	const float target_sat = Lerp(0.27f, 0.32f, dynamic_conf);
	out_params.saturation =
		Clamp(std::pow(target_sat / std::max(out_params.mean_sat_nl, 0.10f), 0.38f), 0.95f, 1.11f);
	out_params.vibrance =
		Clamp(1.0f + (target_sat - out_params.mean_sat_nl) * 0.46f, 0.95f, 1.13f);
	if (out_params.highlight_ratio > 0.08f) {
		out_params.saturation = std::min(out_params.saturation, 1.05f);
		out_params.vibrance = std::min(out_params.vibrance, 1.07f);
	}

	float best_score = -1e9f;
	float best_exposure = 1.0f;
	float best_sat = out_params.mean_sat_nl;
	for (int i = 0; i <= 40; ++i) {
		const float exposure = 0.88f + 0.008f * (float)i;
		GradeParams candidate = out_params;
		candidate.exposure = exposure;
		float mean_sat = 0.0f;
		const float score = EvaluateObjective(analysis, candidate, &mean_sat);
		if (score > best_score) {
			best_score = score;
			best_exposure = exposure;
			best_sat = mean_sat;
		}
	}

	out_params.exposure = best_exposure;
	out_params.objective_score = best_score;
	out_params.detail_amount = 0.0f;

	const auto blend_conf = [&](float v) { return Lerp(1.0f, v, out_params.confidence); };
	out_params.wb_r = blend_conf(out_params.wb_r);
	out_params.wb_g = blend_conf(out_params.wb_g);
	out_params.wb_b = blend_conf(out_params.wb_b);
	out_params.exposure = blend_conf(out_params.exposure);
	out_params.contrast = blend_conf(out_params.contrast);
	out_params.saturation = blend_conf(out_params.saturation);
	out_params.vibrance = blend_conf(out_params.vibrance);
	out_params.shadow_lift *= out_params.confidence;
	out_params.shoulder_strength = Lerp(0.33f, out_params.shoulder_strength, out_params.confidence);

	char detail[640] = {};
	(void)snprintf(detail, sizeof(detail),
		       "solve conf=%.3f score=%.3f exp=%.3f ctr=%.3f sat=%.3f vib=%.3f wb=%.3f/%.3f/%.3f "
		       "dist p05=%.3f p50=%.3f p95=%.3f spread=%.3f sat_out=%.3f",
		       out_params.confidence, out_params.objective_score, out_params.exposure,
		       out_params.contrast, out_params.saturation, out_params.vibrance, out_params.wb_r,
		       out_params.wb_g, out_params.wb_b, out_params.p05, out_params.p50, out_params.p95,
		       out_params.spread, best_sat);
	out_detail = detail;
	return true;
}

} // namespace lenses_autograde::deterministic
