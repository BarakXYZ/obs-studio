#include "filter/host/lenses-filter-internal.h"

#include <util/bmem.h>

static bool lenses_ensure_texrender(gs_texrender_t **render, enum gs_color_format format)
{
	if (*render && gs_texrender_get_format(*render) != format) {
		gs_texrender_destroy(*render);
		*render = NULL;
	}

	if (!*render)
		*render = gs_texrender_create(format, GS_ZS_NONE);

	return *render != NULL;
}

static bool lenses_ensure_fallback_textures(struct lenses_filter_data *filter)
{
	if (!filter)
		return false;

	if (!filter->fallback_overlay_texture) {
		const uint8_t pixel[4] = {0, 0, 0, 0};
		const uint8_t *planes[1] = {pixel};
		filter->fallback_overlay_texture = gs_texture_create(1, 1, GS_BGRA, 1, planes, 0);
	}

	if (!filter->fallback_mask_texture) {
		const uint8_t pixel[1] = {255};
		const uint8_t *planes[1] = {pixel};
		filter->fallback_mask_texture = gs_texture_create(1, 1, GS_A8, 1, planes, 0);
	}

	return filter->fallback_overlay_texture != NULL && filter->fallback_mask_texture != NULL;
}

static void lenses_release_ai_render_targets(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	gs_texrender_destroy(filter->ai_input_texrender);
	filter->ai_input_texrender = NULL;
	gs_stagesurface_destroy(filter->ai_stage_surfaces[0]);
	gs_stagesurface_destroy(filter->ai_stage_surfaces[1]);
	filter->ai_stage_surfaces[0] = NULL;
	filter->ai_stage_surfaces[1] = NULL;
	filter->ai_stage_write_index = 0;
	filter->ai_stage_ready = false;
}

void lenses_destroy_render_targets(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	obs_enter_graphics();
	gs_texrender_destroy(filter->capture_texrender);
	gs_texrender_destroy(filter->ping_texrender);
	gs_texrender_destroy(filter->pong_texrender);
	gs_texrender_destroy(filter->graph_work_a);
	gs_texrender_destroy(filter->graph_work_b);
	gs_texrender_destroy(filter->chain_invert_texrender);
	gs_texrender_destroy(filter->chain_invert_full_texrender);
	gs_texrender_destroy(filter->chain_grayscale_texrender);
	gs_texrender_destroy(filter->chain_solid_texrender);
	gs_texrender_destroy(filter->sync_mask_input_texrender);
	gs_texrender_destroy(filter->sync_mask_luma_texrender);
	for (size_t i = 0; i < LENSES_SYNC_MASK_STAGE_SURFACE_COUNT; ++i)
		gs_stagesurface_destroy(filter->sync_mask_stage_surfaces[i]);
	gs_texrender_destroy(filter->ai_input_texrender);
	gs_stagesurface_destroy(filter->ai_stage_surfaces[0]);
	gs_stagesurface_destroy(filter->ai_stage_surfaces[1]);
	gs_texture_destroy(filter->rule_mask_texture);
	gs_texture_destroy(filter->debug_overlay_texture);
	gs_texture_destroy(filter->fallback_overlay_texture);
	gs_texture_destroy(filter->fallback_mask_texture);
	filter->capture_texrender = NULL;
	filter->ping_texrender = NULL;
	filter->pong_texrender = NULL;
	filter->graph_work_a = NULL;
	filter->graph_work_b = NULL;
	filter->chain_invert_texrender = NULL;
	filter->chain_invert_full_texrender = NULL;
	filter->chain_grayscale_texrender = NULL;
	filter->chain_solid_texrender = NULL;
	filter->sync_mask_input_texrender = NULL;
	filter->sync_mask_luma_texrender = NULL;
	for (size_t i = 0; i < LENSES_SYNC_MASK_STAGE_SURFACE_COUNT; ++i)
		filter->sync_mask_stage_surfaces[i] = NULL;
	filter->sync_mask_stage_write_index = 0;
	filter->sync_mask_stage_pending_frames = 0;
	filter->ai_input_texrender = NULL;
	filter->ai_stage_surfaces[0] = NULL;
	filter->ai_stage_surfaces[1] = NULL;
	filter->rule_mask_texture = NULL;
	filter->rule_mask_width = 0;
	filter->rule_mask_height = 0;
	filter->rule_mask_valid = false;
	filter->debug_overlay_texture = NULL;
	filter->debug_overlay_width = 0;
	filter->debug_overlay_height = 0;
	filter->fallback_overlay_texture = NULL;
	filter->fallback_mask_texture = NULL;
	lenses_invert_component_mask_reset(&filter->invert_component_mask);
	obs_leave_graphics();

	bfree(filter->rule_mask_buffer);
	filter->rule_mask_buffer = NULL;
	filter->rule_mask_buffer_capacity = 0;
	lenses_mask_shape_context_reset(&filter->ai_mask_shape_context);
	bfree(filter->debug_overlay_buffer);
	filter->debug_overlay_buffer = NULL;
	filter->debug_overlay_buffer_capacity = 0;
}

bool lenses_ensure_render_targets(struct lenses_filter_data *filter, enum gs_color_format format)
{
	if (!lenses_ensure_texrender(&filter->capture_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->ping_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->pong_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->graph_work_a, format))
		return false;
	if (!lenses_ensure_texrender(&filter->graph_work_b, format))
		return false;
	if (!lenses_ensure_texrender(&filter->chain_invert_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->chain_invert_full_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->chain_grayscale_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->chain_solid_texrender, format))
		return false;
	if (!lenses_ensure_texrender(&filter->sync_mask_input_texrender, GS_BGRA))
		return false;
	if (!lenses_ensure_texrender(&filter->sync_mask_luma_texrender, GS_R8))
		return false;
	if (lenses_filter_ai_lane_active(filter)) {
		if (!lenses_ensure_texrender(&filter->ai_input_texrender, GS_BGRA))
			return false;
	} else {
		/* Keep AI lane allocations detached when no active policy/debug consumer needs masks. */
		lenses_release_ai_render_targets(filter);
	}
	if (!lenses_ensure_fallback_textures(filter))
		return false;

	return true;
}

const char *lenses_get_technique_and_multiplier(enum gs_color_space current_space,
						enum gs_color_space source_space,
						float *multiplier)
{
	const char *tech_name = "Draw";
	*multiplier = 1.f;

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
		if (current_space == GS_CS_709_SCRGB) {
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		}
		break;
	case GS_CS_709_EXTENDED:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawTonemap";
			break;
		case GS_CS_709_SCRGB:
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
			break;
		default:
			break;
		}
		break;
	case GS_CS_709_SCRGB:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawMultiplyTonemap";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		case GS_CS_709_EXTENDED:
			tech_name = "DrawMultiply";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		default:
			break;
		}
		break;
	}

	return tech_name;
}

void lenses_draw_final_texture(gs_texture_t *texture, uint32_t width, uint32_t height,
			       enum gs_color_space source_space)
{
	if (!texture)
		return;

	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier = 1.f;
	const char *technique = lenses_get_technique_and_multiplier(current_space, source_space, &multiplier);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *multiplier_param = gs_effect_get_param_by_name(effect, "multiplier");
	if (!image_param || !multiplier_param)
		return;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(image_param, texture);
	gs_effect_set_float(multiplier_param, multiplier);

	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(texture, 0, width, height);

	gs_enable_framebuffer_srgb(previous);
}

bool lenses_capture_target(struct lenses_filter_data *filter, obs_source_t *target, obs_source_t *parent,
			   uint32_t width, uint32_t height, enum gs_color_space source_space)
{
	gs_texrender_reset(filter->capture_texrender);
	if (!gs_texrender_begin_with_color_space(filter->capture_texrender, width, height, source_space))
		return false;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	uint32_t parent_flags = obs_source_get_output_flags(parent);
	bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
	bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;

	if (target == parent && !custom_draw && !async)
		obs_source_default_render(target);
	else
		obs_source_video_render(target);

	gs_blend_state_pop();
	gs_texrender_end(filter->capture_texrender);
	return true;
}
