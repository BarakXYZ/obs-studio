#include "filter/host/lenses-filter-internal.h"

#include <util/platform.h>

#include <math.h>

static float clampf(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static void lenses_reset_sync_mask_stage_queue(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	filter->sync_mask_stage_write_index = 0;
	filter->sync_mask_stage_pending_frames = 0;
}

static void lenses_mark_sync_component_mask_unavailable(struct lenses_filter_data *filter)
{
	if (!filter)
		return;
	filter->invert_component_mask.ready = false;
	filter->invert_component_mask.hue_preview_ready = false;
	filter->invert_component_mask.hue_preview_selected_pixels = 0;
	filter->invert_component_mask.hue_preview_selected_coverage = 0.0f;
}

static void lenses_choose_sync_mask_dimensions(uint32_t source_width, uint32_t source_height,
					       uint32_t *out_width, uint32_t *out_height)
{
	if (!out_width || !out_height) {
		return;
	}

	if (source_width == 0 || source_height == 0) {
		*out_width = 0;
		*out_height = 0;
		return;
	}

	/*
	 * Keep sync OpenCV preprocessing responsive by capping the working
	 * resolution while preserving source aspect ratio.
	 */
	const float max_side_target = 512.0f;
	const float max_side = (float)(source_width > source_height ? source_width : source_height);
	float scale = 1.0f;
	if (max_side > max_side_target)
		scale = max_side_target / max_side;

	const uint32_t width = (uint32_t)lroundf((float)source_width * scale);
	const uint32_t height = (uint32_t)lroundf((float)source_height * scale);
	*out_width = clamp_u32(width, 96U, source_width);
	*out_height = clamp_u32(height, 96U, source_height);
}

static bool lenses_render_sync_mask_luma(struct lenses_filter_data *filter, uint32_t width,
					 uint32_t height)
{
	if (!filter || !filter->sync_mask_input_texrender || !filter->sync_mask_luma_texrender)
		return false;

	gs_texture_t *input = gs_texrender_get_texture(filter->sync_mask_input_texrender);
	if (!input)
		return false;

	struct lenses_pass *grayscale_pass = &filter->passes[LENSES_PASS_GRAYSCALE];
	if (!grayscale_pass || !grayscale_pass->effect || !grayscale_pass->image_param)
		return false;

	gs_texrender_reset(filter->sync_mask_luma_texrender);
	if (!gs_texrender_begin(filter->sync_mask_luma_texrender, width, height))
		return false;

	struct vec4 zero;
	vec4_zero(&zero);
	gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear_srgb);
	if (linear_srgb)
		gs_effect_set_texture_srgb(grayscale_pass->image_param, input);
	else
		gs_effect_set_texture(grayscale_pass->image_param, input);
	while (gs_effect_loop(grayscale_pass->effect, "Draw"))
		gs_draw_sprite(input, 0, width, height);
	gs_enable_framebuffer_srgb(previous_srgb);

	gs_blend_state_pop();
	gs_texrender_end(filter->sync_mask_luma_texrender);
	return true;
}

void lenses_update_sync_component_mask(struct lenses_filter_data *filter, gs_texture_t *source_texture,
				       uint32_t source_width, uint32_t source_height,
				       enum gs_color_space source_space)
{
	if (!filter || !source_texture || source_width == 0 || source_height == 0)
		return;

	const bool allow_component_gate =
		filter->invert_enabled && filter->invert_strength > 0.0001f &&
		filter->invert_component_gate_enabled;
	if (!allow_component_gate) {
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	const bool capture_hue_debug_preview =
		filter->debug_enabled && filter->debug_mask_overlay &&
		filter->debug_overlay_mode == LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER;
	const float luma_min = clampf(filter->invert_region.luma_min, 0.0f, 1.0f);
	const float luma_max = clampf(filter->invert_region.luma_max, 0.0f, 1.0f);
	const float saturation_min = clampf(filter->invert_region.saturation_min, 0.0f, 1.0f);
	const float saturation_max = clampf(filter->invert_region.saturation_max, 0.0f, 1.0f);
	const float hue_min_degrees = clampf(filter->invert_region.hue_min_degrees, 0.0f, 360.0f);
	const float hue_max_degrees = clampf(filter->invert_region.hue_max_degrees, 0.0f, 360.0f);
	const bool apply_saturation_gate =
		saturation_min > 0.0001f || saturation_max < 0.9999f;
	const bool apply_hue_qualifier_gate =
		lenses_hue_qualifier_active_band_count(&filter->invert_hue_qualifier) > 0;
	const bool apply_legacy_hue_gate =
		!apply_hue_qualifier_gate && fabsf(hue_max_degrees - hue_min_degrees) < 359.5f;
	const bool requested_luma_fast_path =
		!capture_hue_debug_preview && !apply_saturation_gate && !apply_hue_qualifier_gate &&
		!apply_legacy_hue_gate;

	uint32_t mask_width = 0;
	uint32_t mask_height = 0;
	lenses_choose_sync_mask_dimensions(source_width, source_height, &mask_width, &mask_height);
	if (mask_width == 0 || mask_height == 0) {
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	gs_texrender_reset(filter->sync_mask_input_texrender);
	if (!gs_texrender_begin(filter->sync_mask_input_texrender, mask_width, mask_height)) {
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	struct vec4 zero;
	vec4_zero(&zero);
	gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
	gs_ortho(0.0f, (float)mask_width, 0.0f, (float)mask_height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *multiplier_param = gs_effect_get_param_by_name(effect, "multiplier");
	if (!image_param || !multiplier_param) {
		gs_blend_state_pop();
		gs_texrender_end(filter->sync_mask_input_texrender);
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier = 1.0f;
	const char *technique =
		lenses_get_technique_and_multiplier(current_space, source_space, &multiplier);
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_effect_set_texture_srgb(image_param, source_texture);
	gs_effect_set_float(multiplier_param, multiplier);
	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(source_texture, 0, mask_width, mask_height);
	gs_enable_framebuffer_srgb(previous_srgb);

	gs_blend_state_pop();
	gs_texrender_end(filter->sync_mask_input_texrender);

	bool use_luma_fast_path = requested_luma_fast_path;
	if (use_luma_fast_path && !lenses_render_sync_mask_luma(filter, mask_width, mask_height))
		use_luma_fast_path = false;

	gs_texture_t *stage_texture = use_luma_fast_path
					      ? gs_texrender_get_texture(filter->sync_mask_luma_texrender)
					      : gs_texrender_get_texture(filter->sync_mask_input_texrender);
	const enum gs_color_format stage_format = use_luma_fast_path ? GS_R8 : GS_BGRA;
	if (!stage_texture) {
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	bool queue_invalidated = false;
	for (size_t i = 0; i < LENSES_SYNC_MASK_STAGE_SURFACE_COUNT; ++i) {
		if (!filter->sync_mask_stage_surfaces[i] ||
		    gs_stagesurface_get_width(filter->sync_mask_stage_surfaces[i]) != mask_width ||
		    gs_stagesurface_get_height(filter->sync_mask_stage_surfaces[i]) != mask_height ||
		    gs_stagesurface_get_color_format(filter->sync_mask_stage_surfaces[i]) !=
			    stage_format) {
			gs_stagesurface_destroy(filter->sync_mask_stage_surfaces[i]);
			filter->sync_mask_stage_surfaces[i] =
				gs_stagesurface_create(mask_width, mask_height, stage_format);
			queue_invalidated = true;
		}
	}
	if (queue_invalidated)
		lenses_reset_sync_mask_stage_queue(filter);

	const size_t write_index =
		filter->sync_mask_stage_write_index % LENSES_SYNC_MASK_STAGE_SURFACE_COUNT;
	const size_t read_index = (write_index + 1U) % LENSES_SYNC_MASK_STAGE_SURFACE_COUNT;
	if (!filter->sync_mask_stage_surfaces[write_index] ||
	    !filter->sync_mask_stage_surfaces[read_index]) {
		lenses_mark_sync_component_mask_unavailable(filter);
		lenses_reset_sync_mask_stage_queue(filter);
		return;
	}

	gs_stage_texture(filter->sync_mask_stage_surfaces[write_index], stage_texture);
	filter->sync_mask_stage_write_index =
		(write_index + 1U) % LENSES_SYNC_MASK_STAGE_SURFACE_COUNT;
	/* Keep staged readback delayed by one frame to balance freshness and map stability. */
	if (filter->sync_mask_stage_pending_frames + 1U < LENSES_SYNC_MASK_STAGE_SURFACE_COUNT) {
		filter->sync_mask_stage_pending_frames++;
		return;
	}
	filter->sync_mask_stage_pending_frames = LENSES_SYNC_MASK_STAGE_SURFACE_COUNT - 1U;

	uint8_t *video_data = NULL;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(filter->sync_mask_stage_surfaces[read_index], &video_data, &linesize)) {
		lenses_mark_sync_component_mask_unavailable(filter);
		return;
	}

	const struct lenses_invert_component_mask_params mask_params = {
		.enabled = true,
		.threshold = clampf(filter->invert_region.threshold, 0.0f, 1.0f),
		.edge_softness = clampf(filter->invert_region.softness, 0.0f, 1.0f),
		.topology_softness = LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_DEFAULT,
		.coverage = clampf(filter->invert_region.coverage, 0.0f, 1.0f),
		.luma_min = luma_min,
		.luma_max = luma_max,
		.saturation_min = saturation_min,
		.saturation_max = saturation_max,
		.hue_min_degrees = hue_min_degrees,
		.hue_max_degrees = hue_max_degrees,
		.hue_qualifier = filter->invert_hue_qualifier,
		.min_area_px = clampf(filter->invert_component_min_area_px,
				      LENSES_INVERT_COMPONENT_MIN_AREA_PX_MIN,
				      LENSES_INVERT_COMPONENT_MIN_AREA_PX_MAX),
		.min_side_px = clampf(filter->invert_component_min_side_px,
				      LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MIN,
				      LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MAX),
		.min_fill = clampf(filter->invert_component_min_fill, LENSES_INVERT_COMPONENT_MIN_FILL_MIN,
				   LENSES_INVERT_COMPONENT_MIN_FILL_MAX),
		.min_coverage = clampf(filter->invert_component_min_coverage,
				       LENSES_INVERT_COMPONENT_MIN_COVERAGE_MIN,
				       LENSES_INVERT_COMPONENT_MIN_COVERAGE_MAX),
		.mask_shape =
			{
				.grow_px = clampf(filter->invert_mask_shape.grow_px, 0.0f,
						  LENSES_MASK_GROW_MAX_PX),
				.shrink_px = clampf(filter->invert_mask_shape.shrink_px, 0.0f,
						    LENSES_MASK_SHRINK_MAX_PX),
				.soften_px = clampf(filter->invert_mask_shape.soften_px, 0.0f,
						    LENSES_MASK_SOFTEN_MAX_PX),
			},
		.input_luma_only = use_luma_fast_path,
		.capture_hue_debug_preview = capture_hue_debug_preview,
	};
	(void)lenses_invert_component_mask_update(&filter->invert_component_mask, video_data, mask_width,
						  mask_height, linesize, &mask_params);
	gs_stagesurface_unmap(filter->sync_mask_stage_surfaces[read_index]);
}
