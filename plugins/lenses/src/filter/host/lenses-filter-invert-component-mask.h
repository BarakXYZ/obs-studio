#pragma once

#include "filter/invert/lenses-invert-hue-qualifier.h"
#include "filter/host/lenses-filter-mask-shape.h"

#include <obs-module.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LENSES_INVERT_COMPONENT_MIN_AREA_PX_DEFAULT 3500.0f
#define LENSES_INVERT_COMPONENT_MIN_AREA_PX_MIN 0.0f
#define LENSES_INVERT_COMPONENT_MIN_AREA_PX_MAX 65536.0f

#define LENSES_INVERT_COMPONENT_MIN_SIDE_PX_DEFAULT 8.0f
#define LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MIN 0.0f
#define LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MAX 320.0f

#define LENSES_INVERT_COMPONENT_MIN_FILL_DEFAULT 0.12f
#define LENSES_INVERT_COMPONENT_MIN_FILL_MIN 0.0f
#define LENSES_INVERT_COMPONENT_MIN_FILL_MAX 1.0f

#define LENSES_INVERT_COMPONENT_MIN_COVERAGE_DEFAULT 0.0f
#define LENSES_INVERT_COMPONENT_MIN_COVERAGE_MIN 0.0f
#define LENSES_INVERT_COMPONENT_MIN_COVERAGE_MAX 0.10f

#define LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_DEFAULT 0.24f
#define LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_MIN 0.0f
#define LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_MAX 1.0f

struct lenses_invert_component_mask_params {
	bool enabled;
	float threshold;
	float edge_softness;
	float topology_softness;
	float coverage;
	float luma_min;
	float luma_max;
	float saturation_min;
	float saturation_max;
	float hue_min_degrees;
	float hue_max_degrees;
	struct lenses_invert_hue_range_config hue_qualifier;
	float min_area_px;
	float min_side_px;
	float min_fill;
	float min_coverage;
	struct lenses_mask_shape_params mask_shape;
	bool input_luma_only;
	bool capture_hue_debug_preview;
};

struct lenses_invert_component_mask_context {
	gs_texture_t *texture;
	uint32_t width;
	uint32_t height;
	gs_texture_t *hue_preview_texture;
	uint32_t hue_preview_width;
	uint32_t hue_preview_height;
	uint32_t hue_preview_selected_pixels;
	float hue_preview_selected_coverage;
	bool hue_preview_ready;
	void *opencv_pipeline;
	uint32_t accepted_pixels;
	uint32_t accepted_components;
	float accepted_coverage;
	struct lenses_mask_shape_context mask_shape_context;
	bool ready;
};

void lenses_invert_component_mask_reset(struct lenses_invert_component_mask_context *context);

bool lenses_invert_component_mask_update(struct lenses_invert_component_mask_context *context,
					 const uint8_t *bgra, uint32_t width, uint32_t height,
					 uint32_t linesize,
					 const struct lenses_invert_component_mask_params *params);

#ifdef __cplusplus
}
#endif
