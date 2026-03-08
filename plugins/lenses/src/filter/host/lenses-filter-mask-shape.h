#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LENSES_MASK_GROW_MAX_PX 24.0f
#define LENSES_MASK_SHRINK_MAX_PX 24.0f
#define LENSES_MASK_SOFTEN_MAX_PX 24.0f
#define LENSES_MASK_SHAPE_SLIDER_STEP 0.25f

#ifdef __cplusplus
extern "C" {
#endif

struct lenses_mask_shape_context {
	uint8_t *scratch_a;
	size_t scratch_a_capacity;
	uint8_t *scratch_b;
	size_t scratch_b_capacity;
	uint8_t *scratch_c;
	size_t scratch_c_capacity;
};

struct lenses_mask_shape_params {
	float grow_px;
	float shrink_px;
	float soften_px;
};

void lenses_mask_shape_context_reset(struct lenses_mask_shape_context *ctx);

bool lenses_mask_shape_apply(struct lenses_mask_shape_context *ctx, uint8_t *mask, uint32_t width,
			     uint32_t height, const struct lenses_mask_shape_params *params,
			     bool *out_any_pixels);

#ifdef __cplusplus
}
#endif
