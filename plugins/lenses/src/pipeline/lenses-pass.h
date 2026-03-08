#pragma once

#include <obs-module.h>

struct lenses_filter_data;
struct lenses_pass;

typedef bool (*lenses_pass_enabled_fn)(const struct lenses_filter_data *filter);
typedef bool (*lenses_pass_load_params_fn)(struct lenses_pass *pass);
typedef void (*lenses_pass_free_params_fn)(struct lenses_pass *pass);
typedef void (*lenses_pass_apply_fn)(struct lenses_filter_data *filter, struct lenses_pass *pass);

struct lenses_pass {
	const char *id;
	const char *effect_file;
	gs_effect_t *effect;
	gs_eparam_t *image_param;
	void *data;
	lenses_pass_enabled_fn is_enabled;
	lenses_pass_load_params_fn load_params;
	lenses_pass_free_params_fn free_params;
	lenses_pass_apply_fn apply_params;
};

bool lenses_pass_load(struct lenses_pass *pass);
void lenses_pass_unload(struct lenses_pass *pass);

bool lenses_pass_render(struct lenses_filter_data *filter, struct lenses_pass *pass, gs_texture_t *input,
		       gs_texrender_t *output, uint32_t width, uint32_t height,
		       enum gs_color_space source_space);
