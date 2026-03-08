#include "filter/host/lenses-filter-internal.h"
#include "filter/host/lenses-filter-policy-blend-decision.h"

#include <util/bmem.h>

#include <math.h>
#include <string.h>

static enum lenses_chain_id lenses_chain_from_name(const char *chain_name)
{
	if (!chain_name || !*chain_name || strcmp(chain_name, "passthrough") == 0)
		return LENSES_CHAIN_PASSTHROUGH;
	if (strcmp(chain_name, "invert") == 0)
		return LENSES_CHAIN_INVERT;
	if (strcmp(chain_name, "invert-full") == 0)
		return LENSES_CHAIN_INVERT_FULL;
	if (strcmp(chain_name, "grayscale") == 0)
		return LENSES_CHAIN_GRAYSCALE;
	if (strcmp(chain_name, "solid-red") == 0 || strcmp(chain_name, "solid") == 0)
		return LENSES_CHAIN_SOLID_RED;

	return LENSES_CHAIN_PASSTHROUGH;
}

static float lenses_blend_mode_from_name(const char *blend_mode)
{
	if (!blend_mode || !*blend_mode || strcmp(blend_mode, "replace") == 0)
		return (float)LENSES_BLEND_REPLACE;
	if (strcmp(blend_mode, "alpha_mix") == 0)
		return (float)LENSES_BLEND_ALPHA_MIX;
	if (strcmp(blend_mode, "add") == 0)
		return (float)LENSES_BLEND_ADD;
	if (strcmp(blend_mode, "multiply") == 0)
		return (float)LENSES_BLEND_MULTIPLY;

	return (float)LENSES_BLEND_REPLACE;
}

static bool lenses_ensure_byte_buffer(uint8_t **buffer, size_t *capacity, size_t required)
{
	if (!buffer || !capacity)
		return false;
	if (*capacity >= required)
		return true;

	uint8_t *realloced = brealloc(*buffer, required);
	if (!realloced)
		return false;
	*buffer = realloced;
	*capacity = required;
	return true;
}

static bool lenses_ensure_mask_texture(gs_texture_t **texture, uint32_t *current_width,
				       uint32_t *current_height, uint32_t width,
				       uint32_t height, const uint8_t *data)
{
	if (!texture || !current_width || !current_height || !data || width == 0 || height == 0)
		return false;

	if (*texture && (*current_width != width || *current_height != height)) {
		gs_texture_destroy(*texture);
		*texture = NULL;
		*current_width = 0;
		*current_height = 0;
	}

	if (!*texture) {
		const uint8_t *planes[1] = {data};
		/* Updated every frame via gs_texture_set_image(), so this must be dynamic. */
		*texture = gs_texture_create(width, height, GS_A8, 1, planes, GS_DYNAMIC);
		if (!*texture) {
			return false;
		}
		*current_width = width;
		*current_height = height;
	} else {
		gs_texture_set_image(*texture, data, width, false);
	}
	return true;
}

static bool lenses_refresh_mask_snapshot(struct lenses_filter_data *filter)
{
	if (filter && !filter->ai_enabled) {
		memset(&filter->mask_frame_info, 0, sizeof(filter->mask_frame_info));
		filter->mask_instance_count = 0;
		filter->class_mask_count = 0;
		return false;
	}

	if (!filter || !filter->core) {
		if (filter) {
			memset(&filter->mask_frame_info, 0, sizeof(filter->mask_frame_info));
			filter->mask_instance_count = 0;
			filter->class_mask_count = 0;
		}
		return false;
	}
	if (!lenses_core_get_latest_mask_frame_info(filter->core, &filter->mask_frame_info)) {
		memset(&filter->mask_frame_info, 0, sizeof(filter->mask_frame_info));
		filter->mask_instance_count = 0;
		filter->class_mask_count = 0;
		return false;
	}

	filter->mask_instance_count = lenses_core_copy_latest_mask_instances(
		filter->core, filter->mask_instances, LENSES_MAX_MASK_INSTANCES);
	filter->class_mask_count =
		lenses_core_copy_latest_class_masks(filter->core, filter->class_masks, LENSES_MAX_CLASS_MASKS);
	return true;
}

static uint64_t lenses_find_class_mask_handle(const struct lenses_filter_data *filter, int class_id)
{
	if (!filter)
		return 0;

	for (size_t i = 0; i < filter->class_mask_count; ++i) {
		if (filter->class_masks[i].class_id == class_id)
			return filter->class_masks[i].mask_handle;
	}
	return 0;
}

static bool lenses_load_mask_bitmap(struct lenses_filter_data *filter, uint64_t handle,
				    uint8_t **buffer, size_t *capacity, uint32_t *width,
				    uint32_t *height)
{
	if (!filter || !buffer || !capacity || !width || !height || handle == 0)
		return false;

	size_t required = 0;
	if (!lenses_core_copy_mask_bitmap(filter->core, handle, NULL, 0, width, height, &required))
		return false;
	if (required == 0)
		return false;
	if (!lenses_ensure_byte_buffer(buffer, capacity, required))
		return false;

	return lenses_core_copy_mask_bitmap(filter->core, handle, *buffer, *capacity, width, height,
					    &required);
}

static bool lenses_build_rule_mask(struct lenses_filter_data *filter,
				   const struct lenses_policy_rule_runtime *rule)
{
	if (!filter || !rule)
		return false;

	int selected_class_ids[LENSES_POLICY_MAX_SELECTOR_CLASS_IDS] = {0};
	size_t selected_count = 0;
	if (rule->class_id_count > 0) {
		const size_t limit = rule->class_id_count < LENSES_POLICY_MAX_SELECTOR_CLASS_IDS
					     ? rule->class_id_count
					     : LENSES_POLICY_MAX_SELECTOR_CLASS_IDS;
		for (size_t i = 0; i < limit; ++i)
			selected_class_ids[selected_count++] = rule->class_ids[i];
	} else if (rule->class_id >= 0) {
		selected_class_ids[selected_count++] = rule->class_id;
	}

	if (selected_count == 0)
		return false;

	uint8_t *mask_buffer = NULL;
	size_t mask_capacity = 0;
	uint32_t base_width = 0;
	uint32_t base_height = 0;
	bool any_pixels = false;

	for (size_t i = 0; i < selected_count; ++i) {
		const uint64_t handle = lenses_find_class_mask_handle(filter, selected_class_ids[i]);
		if (handle == 0)
			continue;

		uint32_t width = 0;
		uint32_t height = 0;
		if (!lenses_load_mask_bitmap(filter, handle, &mask_buffer, &mask_capacity, &width, &height))
			continue;
		if (width == 0 || height == 0)
			continue;

		if (base_width == 0 || base_height == 0) {
			base_width = width;
			base_height = height;
			const size_t required = (size_t)base_width * (size_t)base_height;
			if (!lenses_ensure_byte_buffer(&filter->rule_mask_buffer, &filter->rule_mask_buffer_capacity,
						       required)) {
				bfree(mask_buffer);
				return false;
			}
			memset(filter->rule_mask_buffer, 0, required);
		}

		if (width != base_width || height != base_height)
			continue;

		const size_t pixel_count = (size_t)base_width * (size_t)base_height;
		for (size_t p = 0; p < pixel_count; ++p) {
			if (mask_buffer[p] == 0)
				continue;
			filter->rule_mask_buffer[p] = 255;
			any_pixels = true;
		}
	}

	bfree(mask_buffer);
	if (!any_pixels || base_width == 0 || base_height == 0)
		return false;
	const struct lenses_mask_shape_params shape_params = {
		.grow_px = filter->ai_mask_shape.grow_px,
		.shrink_px = filter->ai_mask_shape.shrink_px,
		.soften_px = filter->ai_mask_shape.soften_px,
	};
	if (!lenses_mask_shape_apply(&filter->ai_mask_shape_context, filter->rule_mask_buffer, base_width,
				     base_height, &shape_params, &any_pixels) ||
	    !any_pixels)
		return false;

	return lenses_ensure_mask_texture(&filter->rule_mask_texture, &filter->rule_mask_width,
					  &filter->rule_mask_height, base_width, base_height,
					  filter->rule_mask_buffer);
}

static struct vec4 lenses_debug_color_for_class(int class_id)
{
	static const struct vec4 palette[] = {
		{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 0.9f, 0.3f, 1.0f}, {0.2f, 0.6f, 1.0f, 1.0f},
		{1.0f, 0.7f, 0.2f, 1.0f}, {0.8f, 0.3f, 1.0f, 1.0f}, {0.1f, 0.9f, 0.9f, 1.0f},
	};
	const size_t palette_size = sizeof(palette) / sizeof(palette[0]);
	const size_t index = (size_t)((class_id < 0 ? 0 : class_id) % (int)palette_size);
	return palette[index];
}

static void lenses_fill_debug_marker(uint8_t *overlay, uint32_t width, uint32_t height)
{
	if (!overlay || width == 0 || height == 0)
		return;

	const uint32_t band = (width < 480 || height < 320) ? 12U : 28U;
	for (uint32_t y = 0; y < band && y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const size_t out_index = ((size_t)y * width + x) * 4U;
			const bool stripe = (((x / 8U) + (y / 4U)) % 2U) == 0U;
			overlay[out_index + 0U] = stripe ? 0x20 : 0x10; /* B */
			overlay[out_index + 1U] = stripe ? 0x90 : 0x50; /* G */
			overlay[out_index + 2U] = 0xF0;                  /* R */
			overlay[out_index + 3U] = 210;                   /* A */
		}
	}
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < band && x < width; ++x) {
			const size_t out_index = ((size_t)y * width + x) * 4U;
			const bool stripe = (((x / 4U) + (y / 8U)) % 2U) == 0U;
			overlay[out_index + 0U] = stripe ? 0x20 : 0x10; /* B */
			overlay[out_index + 1U] = stripe ? 0x90 : 0x50; /* G */
			overlay[out_index + 2U] = 0xF0;                  /* R */
			overlay[out_index + 3U] = 210;                   /* A */
		}
	}

	const uint32_t marker_w = width < 120 ? width : 120;
	const uint32_t marker_h = height < 36 ? height : 36;
	for (uint32_t y = 0; y < marker_h; ++y) {
		for (uint32_t x = 0; x < marker_w; ++x) {
			const size_t out_index = ((size_t)y * width + x) * 4U;
			const bool stripe = ((x / 6U) % 2U) == 0U;
			overlay[out_index + 0U] = stripe ? 0x20 : 0x08; /* B */
			overlay[out_index + 1U] = stripe ? 0x90 : 0x40; /* G */
			overlay[out_index + 2U] = 0xF0;                  /* R */
			overlay[out_index + 3U] = 235;                   /* A */
		}
	}

	const uint32_t center_w = width < 220 ? width : 220;
	const uint32_t center_h = height < 64 ? height : 64;
	const uint32_t center_x0 = (width - center_w) / 2U;
	const uint32_t center_y0 = (height - center_h) / 2U;
	for (uint32_t y = 0; y < center_h; ++y) {
		for (uint32_t x = 0; x < center_w; ++x) {
			const uint32_t px = center_x0 + x;
			const uint32_t py = center_y0 + y;
			const size_t out_index = ((size_t)py * width + px) * 4U;
			const bool stripe = (((x / 10U) + (y / 6U)) % 2U) == 0U;
			overlay[out_index + 0U] = stripe ? 0x18 : 0x08; /* B */
			overlay[out_index + 1U] = stripe ? 0x70 : 0x30; /* G */
			overlay[out_index + 2U] = 0xF8;                  /* R */
			overlay[out_index + 3U] = 220;                   /* A */
		}
	}
}

static void lenses_overlay_final_rule_mask_debug(struct lenses_filter_data *filter, uint32_t width,
						 uint32_t height)
{
	if (!filter || !filter->debug_overlay_buffer || !filter->rule_mask_valid ||
	    !filter->rule_mask_buffer)
		return;
	if (filter->rule_mask_width != width || filter->rule_mask_height != height)
		return;

	const uint8_t highlight_b = 0xFF;
	const uint8_t highlight_g = 0xFF;
	const uint8_t highlight_r = 0xFF;
	const size_t pixel_count = (size_t)width * (size_t)height;
	for (size_t p = 0; p < pixel_count; ++p) {
		const uint8_t mask = filter->rule_mask_buffer[p];
		if (mask == 0)
			continue;

		const uint16_t mix = (uint16_t)mask;
		const uint16_t inv = (uint16_t)(255U - mix);
		const size_t out_index = p * 4U;
		filter->debug_overlay_buffer[out_index + 0U] =
			(uint8_t)(((uint16_t)filter->debug_overlay_buffer[out_index + 0U] * inv +
				   (uint16_t)highlight_b * mix + 127U) /
				  255U);
		filter->debug_overlay_buffer[out_index + 1U] =
			(uint8_t)(((uint16_t)filter->debug_overlay_buffer[out_index + 1U] * inv +
				   (uint16_t)highlight_g * mix + 127U) /
				  255U);
		filter->debug_overlay_buffer[out_index + 2U] =
			(uint8_t)(((uint16_t)filter->debug_overlay_buffer[out_index + 2U] * inv +
				   (uint16_t)highlight_r * mix + 127U) /
				  255U);

		const uint8_t mask_alpha = (uint8_t)(((uint16_t)mask * 220U + 127U) / 255U);
		if (mask_alpha > filter->debug_overlay_buffer[out_index + 3U])
			filter->debug_overlay_buffer[out_index + 3U] = mask_alpha;
	}
}

static bool lenses_build_debug_overlay(struct lenses_filter_data *filter, uint32_t render_width,
				       uint32_t render_height)
{
	if (!filter || !filter->debug_enabled || !filter->debug_mask_overlay)
		return false;

	uint32_t base_width = 0;
	uint32_t base_height = 0;
	size_t required = 0;
	if (filter->class_mask_count > 0) {
		bool have_base_mask = false;
		for (size_t i = 0; i < filter->class_mask_count; ++i) {
			const uint64_t handle = filter->class_masks[i].mask_handle;
			if (handle == 0)
				continue;
			if (!lenses_core_copy_mask_bitmap(filter->core, handle, NULL, 0, &base_width,
							  &base_height, &required))
				continue;
			if (base_width == 0 || base_height == 0)
				continue;
			have_base_mask = true;
			break;
		}
		if (!have_base_mask) {
			base_width = render_width;
			base_height = render_height;
			required = (size_t)base_width * (size_t)base_height;
		}
	} else {
		base_width = render_width;
		base_height = render_height;
		required = (size_t)base_width * (size_t)base_height;
	}
	if (base_width == 0 || base_height == 0)
		return false;

	const size_t overlay_required = (size_t)base_width * (size_t)base_height * 4U;
	if (!lenses_ensure_byte_buffer(&filter->debug_overlay_buffer, &filter->debug_overlay_buffer_capacity,
				       overlay_required))
		return false;
	memset(filter->debug_overlay_buffer, 0, overlay_required);

	if (filter->class_mask_count == 0) {
		/* Explicitly show that debug mode is active even when the model yields no detections. */
		lenses_fill_debug_marker(filter->debug_overlay_buffer, base_width, base_height);
	} else {
		uint8_t *mask_buffer = NULL;
		size_t mask_capacity = 0;
		bool painted_any = false;
		for (size_t i = 0; i < filter->class_mask_count; ++i) {
			uint32_t mw = 0;
			uint32_t mh = 0;
			if (!lenses_load_mask_bitmap(filter, filter->class_masks[i].mask_handle, &mask_buffer,
						     &mask_capacity, &mw, &mh))
				continue;
			if (mw != base_width || mh != base_height)
				continue;

			const struct vec4 color = lenses_debug_color_for_class(filter->class_masks[i].class_id);
			const uint8_t cr = (uint8_t)lrintf(color.x * 255.0f);
			const uint8_t cg = (uint8_t)lrintf(color.y * 255.0f);
			const uint8_t cb = (uint8_t)lrintf(color.z * 255.0f);
			for (size_t p = 0; p < (size_t)mw * (size_t)mh; ++p) {
				if (mask_buffer[p] == 0)
					continue;
				const size_t out_index = p * 4U;
				filter->debug_overlay_buffer[out_index + 0U] = cb;
				filter->debug_overlay_buffer[out_index + 1U] = cg;
				filter->debug_overlay_buffer[out_index + 2U] = cr;
				filter->debug_overlay_buffer[out_index + 3U] = 200;
				painted_any = true;
			}
		}
		bfree(mask_buffer);
		mask_buffer = NULL;
		mask_capacity = 0;

		/*
		 * Class masks may be present in metadata but unavailable as bitmaps in rare
		 * races (for example, handle eviction). Keep debug visibly active instead
		 * of rendering a fully transparent overlay.
		 */
		if (!painted_any)
			lenses_fill_debug_marker(filter->debug_overlay_buffer, base_width, base_height);
	}

	lenses_overlay_final_rule_mask_debug(filter, base_width, base_height);

	if (filter->debug_overlay_texture &&
	    (filter->debug_overlay_width != base_width || filter->debug_overlay_height != base_height)) {
		gs_texture_destroy(filter->debug_overlay_texture);
		filter->debug_overlay_texture = NULL;
		filter->debug_overlay_width = 0;
		filter->debug_overlay_height = 0;
	}
	if (!filter->debug_overlay_texture) {
		const uint8_t *planes[1] = {filter->debug_overlay_buffer};
		/* Updated every frame via gs_texture_set_image(), so this must be dynamic. */
		filter->debug_overlay_texture =
			gs_texture_create(base_width, base_height, GS_BGRA, 1, planes, GS_DYNAMIC);
		if (filter->debug_overlay_texture) {
			filter->debug_overlay_width = base_width;
			filter->debug_overlay_height = base_height;
		}
	} else {
		gs_texture_set_image(filter->debug_overlay_texture, filter->debug_overlay_buffer, base_width * 4U,
				     false);
	}

	return filter->debug_overlay_texture != NULL;
}

void lenses_apply_debug_overlay(struct lenses_filter_data *filter, gs_texture_t **io_current_texture,
				size_t *io_blend_passes, uint32_t width, uint32_t height,
				enum gs_color_space source_space, bool refresh_snapshot)
{
	if (!filter || !io_current_texture || !*io_current_texture || !io_blend_passes ||
	    !filter->debug_mask_overlay)
		return;

	if (refresh_snapshot && lenses_filter_debug_requires_ai_masks(filter))
		lenses_refresh_mask_snapshot(filter);

	if (filter->debug_enabled &&
	    filter->debug_overlay_mode == LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER &&
	    filter->invert_component_mask.hue_preview_ready &&
	    filter->invert_component_mask.hue_preview_texture) {
		filter->blend_overlay_texture = filter->invert_component_mask.hue_preview_texture;
		filter->blend_mask_texture = NULL;
		filter->blend_use_mask = false;
		filter->blend_region_mode = 0.0f;
		filter->blend_opacity = fmaxf(0.0f, fminf(1.0f, filter->debug_overlay_opacity));
		filter->blend_mode = (float)LENSES_BLEND_ALPHA_MIX;

		gs_texrender_t *output =
			((*io_blend_passes % 2) == 0) ? filter->graph_work_a : filter->graph_work_b;
		if (!lenses_pass_render(filter, &filter->passes[LENSES_PASS_MASKED_BLEND], *io_current_texture,
					output, width, height, source_space))
			return;

		gs_texture_t *output_texture = gs_texrender_get_texture(output);
		if (!output_texture)
			return;

		*io_current_texture = output_texture;
		(*io_blend_passes)++;
		return;
	}

	const bool built = lenses_build_debug_overlay(filter, width, height);
	if (!built)
		return;

	filter->blend_overlay_texture = filter->debug_overlay_texture;
	filter->blend_mask_texture = NULL;
	filter->blend_use_mask = false;
	filter->blend_region_mode = 0.0f;
	float debug_opacity = fmaxf(0.0f, fminf(1.0f, filter->debug_overlay_opacity));
	/*
	 * Keep no-mask debug marker clearly visible even when persisted opacity
	 * is low from earlier sessions.
	 */
	if (filter->class_mask_count == 0 && debug_opacity < 0.80f)
		debug_opacity = 0.80f;
	filter->blend_opacity = debug_opacity;
	filter->blend_mode = (float)LENSES_BLEND_ALPHA_MIX;

	gs_texrender_t *output =
		((*io_blend_passes % 2) == 0) ? filter->graph_work_a : filter->graph_work_b;
	if (!lenses_pass_render(filter, &filter->passes[LENSES_PASS_MASKED_BLEND], *io_current_texture,
				output, width, height, source_space))
		return;

	gs_texture_t *output_texture = gs_texrender_get_texture(output);
	if (!output_texture)
		return;

	*io_current_texture = output_texture;
	(*io_blend_passes)++;
}

static gs_texture_t *lenses_render_chain_texture(struct lenses_filter_data *filter,
						 enum lenses_chain_id chain_id,
						 gs_texture_t *source_texture, uint32_t width,
						 uint32_t height, enum gs_color_space source_space,
						 gs_texture_t **chain_cache, bool *chain_ready)
{
	if (chain_id < 0 || chain_id > LENSES_CHAIN_SOLID_RED)
		return NULL;

	if (chain_ready[chain_id])
		return chain_cache[chain_id];

	struct lenses_pass *pass = NULL;
	gs_texrender_t *output = NULL;
	switch (chain_id) {
	case LENSES_CHAIN_INVERT:
		pass = &filter->passes[LENSES_PASS_INVERT];
		output = filter->chain_invert_texrender;
		break;
	case LENSES_CHAIN_INVERT_FULL:
		pass = &filter->passes[LENSES_PASS_INVERT_FULL];
		output = filter->chain_invert_full_texrender;
		break;
	case LENSES_CHAIN_GRAYSCALE:
		pass = &filter->passes[LENSES_PASS_GRAYSCALE];
		output = filter->chain_grayscale_texrender;
		break;
	case LENSES_CHAIN_SOLID_RED:
		pass = &filter->passes[LENSES_PASS_SOLID_COLOR];
		output = filter->chain_solid_texrender;
		break;
	case LENSES_CHAIN_PASSTHROUGH:
	default:
		chain_cache[chain_id] = source_texture;
		chain_ready[chain_id] = true;
		return source_texture;
	}

	if (!pass || !output)
		return NULL;

	if (!lenses_pass_render(filter, pass, source_texture, output, width, height, source_space))
		return NULL;

	gs_texture_t *output_texture = gs_texrender_get_texture(output);
	if (!output_texture)
		return NULL;

	chain_cache[chain_id] = output_texture;
	chain_ready[chain_id] = true;
	return output_texture;
}

bool lenses_render_policy_graph(struct lenses_filter_data *filter, gs_texture_t *source_texture,
				uint32_t width, uint32_t height,
				enum gs_color_space source_space, gs_texture_t **out_texture)
{
	if (!filter || !out_texture || !source_texture || !filter->policy_runtime_valid)
		return false;

	gs_texture_t *chain_cache[5] = {0};
	bool chain_ready[5] = {false};
	chain_cache[LENSES_CHAIN_PASSTHROUGH] = source_texture;
	chain_ready[LENSES_CHAIN_PASSTHROUGH] = true;

	enum lenses_chain_id default_chain =
		lenses_chain_from_name(filter->policy_runtime.default_filter_chain);
	gs_texture_t *current_texture =
		lenses_render_chain_texture(filter, default_chain, source_texture, width, height,
					    source_space, chain_cache, chain_ready);
	if (!current_texture)
		current_texture = source_texture;

	size_t blend_passes = 0;
	const bool has_mask_snapshot =
		lenses_filter_needs_ai_masks(filter) && lenses_refresh_mask_snapshot(filter);
	filter->rule_mask_valid = false;
	for (size_t i = 0; i < filter->policy_runtime.rule_count; i++) {
		const struct lenses_policy_rule_runtime *rule = &filter->policy_runtime.rules[i];
		enum lenses_chain_id chain_id = lenses_chain_from_name(rule->filter_chain);
		gs_texture_t *overlay_texture =
			lenses_render_chain_texture(filter, chain_id, source_texture, width, height,
						    source_space, chain_cache, chain_ready);
		if (!overlay_texture || rule->opacity <= 0.0001f)
			continue;

		filter->blend_overlay_texture = overlay_texture;
		filter->blend_mask_texture = NULL;
		filter->blend_use_mask = false;
		const bool targets_masks = lenses_rule_targets_class_masks(rule);
		if (has_mask_snapshot && targets_masks) {
			filter->blend_use_mask = lenses_build_rule_mask(filter, rule);
			if (filter->blend_use_mask)
				filter->rule_mask_valid = true;
		}

		const struct lenses_rule_blend_decision decision =
			lenses_policy_decide_rule_blend(targets_masks, filter->blend_use_mask, rule->region_mode);
		if (decision.skip_rule)
			continue;
		filter->blend_use_mask = decision.use_mask;
		filter->blend_mask_texture = decision.use_mask ? filter->rule_mask_texture : NULL;
		filter->blend_region_mode = decision.region_mode;
		filter->blend_opacity = fmaxf(0.0f, fminf(1.0f, rule->opacity));
		filter->blend_mode = lenses_blend_mode_from_name(rule->blend_mode);

		gs_texrender_t *output = (blend_passes % 2 == 0) ? filter->graph_work_a : filter->graph_work_b;
		if (!lenses_pass_render(filter, &filter->passes[LENSES_PASS_MASKED_BLEND], current_texture,
					output, width, height, source_space))
			continue;

		gs_texture_t *output_texture = gs_texrender_get_texture(output);
		if (!output_texture)
			continue;

		current_texture = output_texture;
		blend_passes++;
	}

	lenses_apply_debug_overlay(filter, &current_texture, &blend_passes, width, height, source_space,
				   false);

	filter->blend_overlay_texture = NULL;
	filter->blend_mask_texture = NULL;
	filter->blend_use_mask = false;
	filter->blend_region_mode = 0.0f;
	*out_texture = current_texture;
	return true;
}
