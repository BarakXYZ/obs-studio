#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LENSES_INVERT_HUE_RANGE_MAX_BANDS 6U

enum lenses_invert_hue_range_mode {
	LENSES_INVERT_HUE_RANGE_MODE_INCLUDE = 0,
	LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE = 1,
};

struct lenses_invert_hue_range_band {
	uint8_t enabled;
	float center_degrees;
	float width_degrees;
	float softness_degrees;
};

struct lenses_invert_hue_range_config {
	uint8_t enabled;
	uint32_t mode;
	struct lenses_invert_hue_range_band bands[LENSES_INVERT_HUE_RANGE_MAX_BANDS];
};

#ifdef __cplusplus
}
#endif

