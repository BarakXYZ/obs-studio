#include "filter/host/lenses-filter-hue-presets.h"

#include <math.h>
#include <string.h>

#define LENSES_HUE_DEFAULT_MODE LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE

#define LENSES_HUE_BAND_DISABLED                                                                     \
	{                                                                                            \
		0, 0.0f, 24.0f, 12.0f                                                               \
	}
#define LENSES_HUE_BAND_SKY                                                                          \
	{                                                                                            \
		1, 210.0f, 84.0f, 16.0f                                                            \
	}
#define LENSES_HUE_BAND_FOLIAGE                                                                      \
	{                                                                                            \
		1, 118.0f, 92.0f, 18.0f                                                            \
	}
#define LENSES_HUE_BAND_SKIN                                                                         \
	{                                                                                            \
		1, 20.0f, 72.0f, 14.0f                                                             \
	}

static const struct lenses_hue_preset_definition k_hue_presets[] = {
	{
		.id = LENSES_HUE_PRESET_NATURAL_GUARDS,
		.label_key = "LensesFilter.InvertHuePreset.Natural",
		.description_key = "LensesFilter.InvertHuePreset.Natural.Description",
		.qualifier_enabled = 1,
		.mode = LENSES_HUE_DEFAULT_MODE,
		.bands = {LENSES_HUE_BAND_SKY, LENSES_HUE_BAND_FOLIAGE, LENSES_HUE_BAND_SKIN,
			  LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED},
	},
	{
		.id = LENSES_HUE_PRESET_SKY_GUARD,
		.label_key = "LensesFilter.InvertHuePreset.Sky",
		.description_key = "LensesFilter.InvertHuePreset.Sky.Description",
		.qualifier_enabled = 1,
		.mode = LENSES_HUE_DEFAULT_MODE,
		.bands = {LENSES_HUE_BAND_SKY, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED,
			  LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED},
	},
	{
		.id = LENSES_HUE_PRESET_FOLIAGE_GUARD,
		.label_key = "LensesFilter.InvertHuePreset.Foliage",
		.description_key = "LensesFilter.InvertHuePreset.Foliage.Description",
		.qualifier_enabled = 1,
		.mode = LENSES_HUE_DEFAULT_MODE,
		.bands = {LENSES_HUE_BAND_FOLIAGE, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED,
			  LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED},
	},
	{
		.id = LENSES_HUE_PRESET_SKIN_GUARD,
		.label_key = "LensesFilter.InvertHuePreset.Skin",
		.description_key = "LensesFilter.InvertHuePreset.Skin.Description",
		.qualifier_enabled = 1,
		.mode = LENSES_HUE_DEFAULT_MODE,
		.bands = {LENSES_HUE_BAND_SKIN, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED,
			  LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED, LENSES_HUE_BAND_DISABLED},
	},
};

static const struct lenses_hue_band_template k_hue_band_templates[] = {
	{
		.id = LENSES_HUE_BAND_TEMPLATE_SKY,
		.label_key = "LensesFilter.InvertHueTemplate.Sky",
		.description_key = "LensesFilter.InvertHueTemplate.Sky.Description",
		.band = LENSES_HUE_BAND_SKY,
	},
	{
		.id = LENSES_HUE_BAND_TEMPLATE_FOLIAGE,
		.label_key = "LensesFilter.InvertHueTemplate.Foliage",
		.description_key = "LensesFilter.InvertHueTemplate.Foliage.Description",
		.band = LENSES_HUE_BAND_FOLIAGE,
	},
	{
		.id = LENSES_HUE_BAND_TEMPLATE_SKIN,
		.label_key = "LensesFilter.InvertHueTemplate.Skin",
		.description_key = "LensesFilter.InvertHueTemplate.Skin.Description",
		.band = LENSES_HUE_BAND_SKIN,
	},
};

static const struct lenses_hue_preset_definition *preset_by_id(enum lenses_hue_preset_id id)
{
	for (size_t i = 0; i < (sizeof(k_hue_presets) / sizeof(k_hue_presets[0])); ++i) {
		if (k_hue_presets[i].id == id)
			return &k_hue_presets[i];
	}
	return NULL;
}

size_t lenses_hue_preset_count(void)
{
	return sizeof(k_hue_presets) / sizeof(k_hue_presets[0]);
}

const struct lenses_hue_preset_definition *lenses_hue_preset_at(size_t index)
{
	if (index >= lenses_hue_preset_count())
		return NULL;
	return &k_hue_presets[index];
}

const struct lenses_hue_preset_definition *lenses_hue_preset_default(void)
{
	return &k_hue_presets[0];
}

bool lenses_hue_preset_apply(enum lenses_hue_preset_id id, bool *out_qualifier_enabled,
			     uint32_t *out_mode, struct lenses_invert_hue_range_config *out_config)
{
	if (!out_qualifier_enabled || !out_mode || !out_config)
		return false;

	const struct lenses_hue_preset_definition *preset = preset_by_id(id);
	if (!preset)
		return false;

	*out_qualifier_enabled = preset->qualifier_enabled != 0;
	*out_mode = preset->mode;
	memset(out_config, 0, sizeof(*out_config));
	out_config->enabled = preset->qualifier_enabled;
	out_config->mode = preset->mode;
	for (size_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i)
		out_config->bands[i] = preset->bands[i];
	return true;
}

static bool band_matches(const struct lenses_invert_hue_range_band *lhs,
			 const struct lenses_invert_hue_range_band *rhs)
{
	const float epsilon = 0.25f;
	if (lhs->enabled != rhs->enabled)
		return false;
	if (fabsf(lhs->center_degrees - rhs->center_degrees) > epsilon)
		return false;
	if (fabsf(lhs->width_degrees - rhs->width_degrees) > epsilon)
		return false;
	if (fabsf(lhs->softness_degrees - rhs->softness_degrees) > epsilon)
		return false;
	return true;
}

enum lenses_hue_preset_id lenses_hue_preset_detect(bool qualifier_enabled, uint32_t mode,
						    const struct lenses_invert_hue_range_config *config)
{
	if (!config)
		return LENSES_HUE_PRESET_CUSTOM;

	for (size_t i = 0; i < lenses_hue_preset_count(); ++i) {
		const struct lenses_hue_preset_definition *preset = &k_hue_presets[i];
		if ((preset->qualifier_enabled != 0) != qualifier_enabled)
			continue;
		if (preset->mode != mode)
			continue;

		bool match = true;
		for (size_t band = 0; band < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++band) {
			if (!band_matches(&preset->bands[band], &config->bands[band])) {
				match = false;
				break;
			}
		}
		if (match)
			return preset->id;
	}

	return LENSES_HUE_PRESET_CUSTOM;
}

size_t lenses_hue_band_template_count(void)
{
	return sizeof(k_hue_band_templates) / sizeof(k_hue_band_templates[0]);
}

const struct lenses_hue_band_template *lenses_hue_band_template_at(size_t index)
{
	if (index >= lenses_hue_band_template_count())
		return NULL;
	return &k_hue_band_templates[index];
}
