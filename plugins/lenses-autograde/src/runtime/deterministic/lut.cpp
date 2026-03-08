#include "runtime/deterministic/lut.hpp"

#include <algorithm>
#include <cmath>

namespace lenses_autograde::deterministic {

bool BuildLutFromParams(uint32_t lut_dim, const GradeParams &params, std::vector<float> &out_lut,
			float &out_mean_delta, float &out_max_delta)
{
	out_lut.clear();
	out_mean_delta = 0.0f;
	out_max_delta = 0.0f;
	if (lut_dim < 2U)
		return false;

	const size_t voxel_count = (size_t)lut_dim * (size_t)lut_dim * (size_t)lut_dim;
	out_lut.assign(voxel_count * 3U, 0.0f);

	const float inv_dim_minus = 1.0f / (float)(lut_dim - 1U);
	double delta_sum = 0.0;
	float max_delta = 0.0f;

	for (uint32_t b = 0; b < lut_dim; ++b) {
		for (uint32_t g = 0; g < lut_dim; ++g) {
			for (uint32_t r = 0; r < lut_dim; ++r) {
				const size_t idx =
					(size_t)r + (size_t)g * lut_dim + (size_t)b * lut_dim * lut_dim;
				const Rgb in_nl = {
					.r = (float)r * inv_dim_minus,
					.g = (float)g * inv_dim_minus,
					.b = (float)b * inv_dim_minus,
				};
				const Rgb out = ApplyTransformToLinear(in_nl, params);
				out_lut[idx] = out.r;
				out_lut[voxel_count + idx] = out.g;
				out_lut[voxel_count * 2U + idx] = out.b;

				const float id_r = SrgbToLinear(in_nl.r);
				const float id_g = SrgbToLinear(in_nl.g);
				const float id_b = SrgbToLinear(in_nl.b);
				const float dr = std::fabs(out.r - id_r);
				const float dg = std::fabs(out.g - id_g);
				const float db = std::fabs(out.b - id_b);
				delta_sum += (double)(dr + dg + db);
				max_delta = std::max(max_delta, std::max(dr, std::max(dg, db)));
			}
		}
	}

	out_mean_delta = (float)(delta_sum / ((double)voxel_count * 3.0));
	out_max_delta = max_delta;
	return true;
}

float ConstrainLutToIdentity(std::vector<float> &lut, uint32_t lut_dim, float max_allowed_delta,
			     float &out_mean_delta, float &out_max_delta)
{
	const size_t voxel_count = (size_t)lut_dim * (size_t)lut_dim * (size_t)lut_dim;
	const float inv_dim_minus = 1.0f / (float)(lut_dim - 1U);
	auto eval_delta = [&](float &mean_delta, float &max_delta) {
		double sum = 0.0;
		float max_v = 0.0f;
		for (size_t idx = 0; idx < voxel_count; ++idx) {
			const uint32_t r = (uint32_t)(idx % lut_dim);
			const uint32_t g = (uint32_t)((idx / lut_dim) % lut_dim);
			const uint32_t b = (uint32_t)(idx / ((size_t)lut_dim * (size_t)lut_dim));
			const float id_r = SrgbToLinear((float)r * inv_dim_minus);
			const float id_g = SrgbToLinear((float)g * inv_dim_minus);
			const float id_b = SrgbToLinear((float)b * inv_dim_minus);
			const float dr = std::fabs(lut[idx] - id_r);
			const float dg = std::fabs(lut[voxel_count + idx] - id_g);
			const float db = std::fabs(lut[voxel_count * 2U + idx] - id_b);
			sum += (double)(dr + dg + db);
			max_v = std::max(max_v, std::max(dr, std::max(dg, db)));
		}
		mean_delta = (float)(sum / ((double)voxel_count * 3.0));
		max_delta = max_v;
	};

	float mean_before = 0.0f;
	float max_before = 0.0f;
	eval_delta(mean_before, max_before);
	if (max_before <= max_allowed_delta + 1e-6f) {
		out_mean_delta = mean_before;
		out_max_delta = max_before;
		return 1.0f;
	}

	const float mix = Clamp(max_allowed_delta / std::max(max_before, 1e-6f), 0.0f, 1.0f);
	for (size_t idx = 0; idx < voxel_count; ++idx) {
		const uint32_t r = (uint32_t)(idx % lut_dim);
		const uint32_t g = (uint32_t)((idx / lut_dim) % lut_dim);
		const uint32_t b = (uint32_t)(idx / ((size_t)lut_dim * (size_t)lut_dim));
		const float id_r = SrgbToLinear((float)r * inv_dim_minus);
		const float id_g = SrgbToLinear((float)g * inv_dim_minus);
		const float id_b = SrgbToLinear((float)b * inv_dim_minus);
		lut[idx] = Lerp(id_r, lut[idx], mix);
		lut[voxel_count + idx] = Lerp(id_g, lut[voxel_count + idx], mix);
		lut[voxel_count * 2U + idx] = Lerp(id_b, lut[voxel_count * 2U + idx], mix);
	}

	eval_delta(out_mean_delta, out_max_delta);
	return mix;
}

} // namespace lenses_autograde::deterministic
