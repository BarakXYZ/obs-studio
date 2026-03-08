#include "pipeline/lenses-pass.h"

#include <graphics/vec4.h>
#include <util/platform.h>

static void lenses_pass_reset(struct lenses_pass *pass)
{
	pass->effect = NULL;
	pass->image_param = NULL;
	pass->data = NULL;
}

bool lenses_pass_load(struct lenses_pass *pass)
{
	char *effect_path = NULL;
	bool loaded = false;

	if (!pass || !pass->effect_file)
		return false;

	effect_path = obs_module_file(pass->effect_file);
	if (!effect_path)
		return false;

	obs_enter_graphics();
	pass->effect = gs_effect_create_from_file(effect_path, NULL);
	if (pass->effect)
		pass->image_param = gs_effect_get_param_by_name(pass->effect, "image");
	obs_leave_graphics();

	if (!pass->effect) {
		blog(LOG_WARNING, "[lenses] Failed to load effect for pass '%s' from '%s'", pass->id, pass->effect_file);
		goto cleanup;
	}

	if (!pass->image_param) {
		blog(LOG_WARNING, "[lenses] Effect for pass '%s' is missing required 'image' uniform", pass->id);
		goto cleanup;
	}

	if (pass->load_params && !pass->load_params(pass)) {
		blog(LOG_WARNING, "[lenses] Failed to initialize effect parameters for pass '%s'", pass->id);
		goto cleanup;
	}

	loaded = true;

cleanup:
	bfree(effect_path);

	if (!loaded)
		lenses_pass_unload(pass);

	return loaded;
}

void lenses_pass_unload(struct lenses_pass *pass)
{
	if (!pass)
		return;

	if (pass->free_params)
		pass->free_params(pass);

	if (pass->effect) {
		obs_enter_graphics();
		gs_effect_destroy(pass->effect);
		obs_leave_graphics();
	}

	lenses_pass_reset(pass);
}

bool lenses_pass_render(struct lenses_filter_data *filter, struct lenses_pass *pass, gs_texture_t *input,
		       gs_texrender_t *output, uint32_t width, uint32_t height,
		       enum gs_color_space source_space)
{
	UNUSED_PARAMETER(filter);

	if (!pass || !pass->effect || !pass->image_param || !input || !output || !width || !height)
		return false;

	gs_texrender_reset(output);
	if (!gs_texrender_begin_with_color_space(output, width, height, source_space))
		return false;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear_srgb);

	if (linear_srgb)
		gs_effect_set_texture_srgb(pass->image_param, input);
	else
		gs_effect_set_texture(pass->image_param, input);

	if (pass->apply_params)
		pass->apply_params(filter, pass);

	while (gs_effect_loop(pass->effect, "Draw"))
		gs_draw_sprite(input, 0, width, height);

	gs_enable_framebuffer_srgb(previous);
	gs_blend_state_pop();

	gs_texrender_end(output);
	return true;
}
