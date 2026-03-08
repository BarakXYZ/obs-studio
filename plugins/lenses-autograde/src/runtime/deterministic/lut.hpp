#pragma once

#include "runtime/deterministic/params.hpp"

#include <cstdint>
#include <vector>

namespace lenses_autograde::deterministic {

bool BuildLutFromParams(uint32_t lut_dim, const GradeParams &params, std::vector<float> &out_lut,
			float &out_mean_delta, float &out_max_delta);

float ConstrainLutToIdentity(std::vector<float> &lut, uint32_t lut_dim, float max_allowed_delta,
			     float &out_mean_delta, float &out_max_delta);

} // namespace lenses_autograde::deterministic
