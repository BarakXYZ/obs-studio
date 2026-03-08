#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lenses_autograde {

enum class DeterministicInputTransferHint : uint32_t {
	Auto = 0,
	SrgbNonlinear = 1,
	Linear = 2,
};

struct DeterministicGradeOutput {
	std::vector<float> lut;
	float detail_amount = 0.0f;
	std::string detail;
};

bool BuildDeterministicGradeFromBgra(const uint8_t *bgra, uint32_t width, uint32_t height,
				     uint32_t linesize, uint32_t lut_dim,
				     DeterministicInputTransferHint transfer_hint,
				     DeterministicGradeOutput &out_grade);

inline bool BuildDeterministicGradeFromBgra(const uint8_t *bgra, uint32_t width, uint32_t height,
					    uint32_t linesize, uint32_t lut_dim,
					    DeterministicGradeOutput &out_grade)
{
	return BuildDeterministicGradeFromBgra(bgra, width, height, linesize, lut_dim,
					       DeterministicInputTransferHint::Auto, out_grade);
}

} // namespace lenses_autograde
