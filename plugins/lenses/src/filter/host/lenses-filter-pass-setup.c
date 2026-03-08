#include "filter/host/lenses-filter-internal.h"

#include <util/bmem.h>

#include <string.h>

static bool invert_pass_enabled(const struct lenses_filter_data *filter)
{
	return filter->invert_enabled && filter->invert_strength > 0.0001f;
}

static bool invert_pass_load_params(struct lenses_pass *pass)
{
	struct invert_pass_params *params = bzalloc(sizeof(*params));
	params->invert_strength = gs_effect_get_param_by_name(pass->effect, "invert_strength");
	params->region_component_mask =
		gs_effect_get_param_by_name(pass->effect, "region_component_mask");
	params->use_region_component_mask =
		gs_effect_get_param_by_name(pass->effect, "use_region_component_mask");

	if (!params->invert_strength || !params->region_component_mask ||
	    !params->use_region_component_mask) {
		bfree(params);
		return false;
	}
	params->apply_region_component_mask = strcmp(pass->id, "invert-full") != 0;

	pass->data = params;
	return true;
}

static void invert_pass_free_params(struct lenses_pass *pass)
{
	if (!pass)
		return;

	bfree(pass->data);
	pass->data = NULL;
}

static void invert_pass_apply_params(struct lenses_filter_data *filter, struct lenses_pass *pass)
{
	struct invert_pass_params *params = pass->data;
	if (!params)
		return;

	gs_effect_set_float(params->invert_strength, filter->smoothed_invert_strength);
	gs_texture_t *component_mask = filter->invert_component_mask.texture
					       ? filter->invert_component_mask.texture
					       : filter->fallback_mask_texture;
	if (component_mask)
		gs_effect_set_texture(params->region_component_mask, component_mask);
	const bool use_region_component_mask =
		params->apply_region_component_mask && filter->invert_component_gate_enabled &&
		filter->invert_component_mask.ready && filter->invert_component_mask.texture;
	gs_effect_set_float(params->use_region_component_mask, use_region_component_mask ? 1.0f : 0.0f);
}

static bool grayscale_pass_load_params(struct lenses_pass *pass)
{
	UNUSED_PARAMETER(pass);
	return true;
}

static bool solid_color_pass_load_params(struct lenses_pass *pass)
{
	struct solid_color_pass_params *params = bzalloc(sizeof(*params));
	params->solid_color = gs_effect_get_param_by_name(pass->effect, "solid_color");
	if (!params->solid_color) {
		bfree(params);
		return false;
	}

	pass->data = params;
	return true;
}

static void solid_color_pass_free_params(struct lenses_pass *pass)
{
	if (!pass)
		return;

	bfree(pass->data);
	pass->data = NULL;
}

static void solid_color_pass_apply_params(struct lenses_filter_data *filter, struct lenses_pass *pass)
{
	UNUSED_PARAMETER(filter);
	struct solid_color_pass_params *params = pass->data;
	if (!params)
		return;

	struct vec4 color;
	vec4_set(&color, 1.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(params->solid_color, &color);
}

static bool masked_blend_pass_load_params(struct lenses_pass *pass)
{
	struct masked_blend_pass_params *params = bzalloc(sizeof(*params));
	params->overlay_image = gs_effect_get_param_by_name(pass->effect, "overlay_image");
	params->mask_image = gs_effect_get_param_by_name(pass->effect, "mask_image");
	params->opacity = gs_effect_get_param_by_name(pass->effect, "opacity");
	params->blend_mode = gs_effect_get_param_by_name(pass->effect, "blend_mode");
	params->use_mask = gs_effect_get_param_by_name(pass->effect, "use_mask");
	params->region_mode = gs_effect_get_param_by_name(pass->effect, "region_mode");
	if (!params->overlay_image || !params->mask_image || !params->opacity || !params->blend_mode ||
	    !params->use_mask || !params->region_mode) {
		bfree(params);
		return false;
	}

	pass->data = params;
	return true;
}

static void masked_blend_pass_free_params(struct lenses_pass *pass)
{
	if (!pass)
		return;

	bfree(pass->data);
	pass->data = NULL;
}

static void masked_blend_pass_apply_params(struct lenses_filter_data *filter, struct lenses_pass *pass)
{
	struct masked_blend_pass_params *params = pass->data;
	if (!params || !filter)
		return;

	gs_texture_t *overlay =
		filter->blend_overlay_texture ? filter->blend_overlay_texture : filter->fallback_overlay_texture;
	gs_texture_t *mask = filter->blend_mask_texture ? filter->blend_mask_texture : filter->fallback_mask_texture;
	if (overlay)
		gs_effect_set_texture(params->overlay_image, overlay);
	if (mask)
		gs_effect_set_texture(params->mask_image, mask);
	gs_effect_set_float(params->opacity, filter->blend_opacity);
	gs_effect_set_float(params->blend_mode, filter->blend_mode);
	gs_effect_set_float(params->use_mask, filter->blend_use_mask ? 1.0f : 0.0f);
	gs_effect_set_float(params->region_mode, filter->blend_region_mode);
}

bool lenses_initialize_passes(struct lenses_filter_data *filter)
{
	struct lenses_pass *invert_pass = &filter->passes[LENSES_PASS_INVERT];
	*invert_pass = (struct lenses_pass){
		.id = "invert",
		.effect_file = "effects/invert.effect",
		.is_enabled = invert_pass_enabled,
		.load_params = invert_pass_load_params,
		.free_params = invert_pass_free_params,
		.apply_params = invert_pass_apply_params,
	};

	struct lenses_pass *grayscale_pass = &filter->passes[LENSES_PASS_GRAYSCALE];
	*grayscale_pass = (struct lenses_pass){
		.id = "grayscale",
		.effect_file = "effects/grayscale.effect",
		.load_params = grayscale_pass_load_params,
	};

	struct lenses_pass *invert_full_pass = &filter->passes[LENSES_PASS_INVERT_FULL];
	*invert_full_pass = (struct lenses_pass){
		.id = "invert-full",
		.effect_file = "effects/invert.effect",
		.is_enabled = invert_pass_enabled,
		.load_params = invert_pass_load_params,
		.free_params = invert_pass_free_params,
		.apply_params = invert_pass_apply_params,
	};

	struct lenses_pass *solid_pass = &filter->passes[LENSES_PASS_SOLID_COLOR];
	*solid_pass = (struct lenses_pass){
		.id = "solid-red",
		.effect_file = "effects/solid-color.effect",
		.load_params = solid_color_pass_load_params,
		.free_params = solid_color_pass_free_params,
		.apply_params = solid_color_pass_apply_params,
	};

	struct lenses_pass *masked_blend_pass = &filter->passes[LENSES_PASS_MASKED_BLEND];
	*masked_blend_pass = (struct lenses_pass){
		.id = "masked-blend",
		.effect_file = "effects/masked-blend.effect",
		.load_params = masked_blend_pass_load_params,
		.free_params = masked_blend_pass_free_params,
		.apply_params = masked_blend_pass_apply_params,
	};

	filter->pass_count = LENSES_PASS_COUNT;
	for (size_t i = 0; i < filter->pass_count; i++) {
		if (lenses_pass_load(&filter->passes[i]))
			continue;

		blog(LOG_WARNING, "[lenses] Failed to initialize pass '%s'", filter->passes[i].id);
		return false;
	}

	return true;
}

void lenses_destroy_passes(struct lenses_filter_data *filter)
{
	for (size_t i = 0; i < filter->pass_count; i++)
		lenses_pass_unload(&filter->passes[i]);

	filter->pass_count = 0;
}
