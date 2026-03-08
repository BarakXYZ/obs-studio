#include "filter/host/lenses-filter-hue-qualifier.h"
#include "filter/host/lenses-filter-hue-presets.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const k_band_enabled_keys[LENSES_INVERT_HUE_RANGE_MAX_BANDS] = {
	"invert_hue_band_1_enabled",
	"invert_hue_band_2_enabled",
	"invert_hue_band_3_enabled",
	"invert_hue_band_4_enabled",
	"invert_hue_band_5_enabled",
	"invert_hue_band_6_enabled",
};

static const char *const k_band_center_keys[LENSES_INVERT_HUE_RANGE_MAX_BANDS] = {
	"invert_hue_band_1_center_degrees",
	"invert_hue_band_2_center_degrees",
	"invert_hue_band_3_center_degrees",
	"invert_hue_band_4_center_degrees",
	"invert_hue_band_5_center_degrees",
	"invert_hue_band_6_center_degrees",
};

static const char *const k_band_width_keys[LENSES_INVERT_HUE_RANGE_MAX_BANDS] = {
	"invert_hue_band_1_width_degrees",
	"invert_hue_band_2_width_degrees",
	"invert_hue_band_3_width_degrees",
	"invert_hue_band_4_width_degrees",
	"invert_hue_band_5_width_degrees",
	"invert_hue_band_6_width_degrees",
};

static const char *const k_band_softness_keys[LENSES_INVERT_HUE_RANGE_MAX_BANDS] = {
	"invert_hue_band_1_softness_degrees",
	"invert_hue_band_2_softness_degrees",
	"invert_hue_band_3_softness_degrees",
	"invert_hue_band_4_softness_degrees",
	"invert_hue_band_5_softness_degrees",
	"invert_hue_band_6_softness_degrees",
};

static const struct lenses_hue_preset_definition *default_hue_preset(void)
{
	const struct lenses_hue_preset_definition *preset = lenses_hue_preset_default();
	if (preset)
		return preset;
	return lenses_hue_preset_at(0);
}

static struct lenses_invert_hue_range_band default_band(uint32_t band_index)
{
	const struct lenses_invert_hue_range_band fallback = {
		.enabled = 0,
		.center_degrees = LENSES_INVERT_HUE_RANGE_CENTER_DEFAULT,
		.width_degrees = LENSES_INVERT_HUE_RANGE_WIDTH_DEFAULT,
		.softness_degrees = LENSES_INVERT_HUE_RANGE_SOFTNESS_DEFAULT,
	};

	const struct lenses_hue_preset_definition *preset = default_hue_preset();
	if (!preset || band_index >= LENSES_INVERT_HUE_RANGE_MAX_BANDS)
		return fallback;
	return preset->bands[band_index];
}

static float clampf(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static float normalize_degrees(float degrees)
{
	if (!isfinite(degrees))
		return 0.0f;

	float normalized = fmodf(degrees, 360.0f);
	if (normalized < 0.0f)
		normalized += 360.0f;
	if (normalized >= 360.0f)
		normalized -= 360.0f;
	return normalized;
}

const char *lenses_hue_band_setting_key(enum lenses_hue_band_setting_field field, uint32_t band_index)
{
	if (band_index >= LENSES_INVERT_HUE_RANGE_MAX_BANDS)
		return NULL;

	switch (field) {
	case LENSES_HUE_BAND_SETTING_ENABLED:
		return k_band_enabled_keys[band_index];
	case LENSES_HUE_BAND_SETTING_CENTER_DEGREES:
		return k_band_center_keys[band_index];
	case LENSES_HUE_BAND_SETTING_WIDTH_DEGREES:
		return k_band_width_keys[band_index];
	case LENSES_HUE_BAND_SETTING_SOFTNESS_DEGREES:
		return k_band_softness_keys[band_index];
	default:
		return NULL;
	}
}

void lenses_hue_qualifier_set_default_settings(obs_data_t *settings)
{
	if (!settings)
		return;

	obs_data_set_default_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED,
				  LENSES_INVERT_HUE_QUALIFIER_DEFAULT_ENABLED);
	const struct lenses_hue_preset_definition *preset = default_hue_preset();
	const int default_mode = preset ? (int)preset->mode : LENSES_INVERT_HUE_QUALIFIER_DEFAULT_MODE;
	obs_data_set_default_int(settings, SETTING_INVERT_HUE_QUALIFIER_MODE,
				 default_mode);

	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		const struct lenses_invert_hue_range_band band = default_band(i);
		obs_data_set_default_bool(settings, k_band_enabled_keys[i], band.enabled != 0);
		obs_data_set_default_double(settings, k_band_center_keys[i], band.center_degrees);
		obs_data_set_default_double(settings, k_band_width_keys[i], band.width_degrees);
		obs_data_set_default_double(settings, k_band_softness_keys[i], band.softness_degrees);
	}
}

void lenses_hue_qualifier_clamp(struct lenses_invert_hue_range_config *config)
{
	if (!config)
		return;

	config->enabled = config->enabled != 0 ? 1 : 0;
	if (config->mode != LENSES_INVERT_HUE_RANGE_MODE_INCLUDE &&
	    config->mode != LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE) {
		config->mode = LENSES_INVERT_HUE_QUALIFIER_DEFAULT_MODE;
	}

	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		struct lenses_invert_hue_range_band *band = &config->bands[i];
		band->enabled = band->enabled != 0 ? 1 : 0;
		band->center_degrees =
			normalize_degrees(clampf(band->center_degrees, LENSES_INVERT_HUE_RANGE_CENTER_MIN,
					       LENSES_INVERT_HUE_RANGE_CENTER_MAX));
		band->width_degrees = clampf(band->width_degrees, LENSES_INVERT_HUE_RANGE_WIDTH_MIN,
					     LENSES_INVERT_HUE_RANGE_WIDTH_MAX);
		band->softness_degrees =
			clampf(band->softness_degrees, LENSES_INVERT_HUE_RANGE_SOFTNESS_MIN,
			       LENSES_INVERT_HUE_RANGE_SOFTNESS_MAX);
	}
}

void lenses_hue_qualifier_load_settings(obs_data_t *settings,
					struct lenses_invert_hue_range_config *out_config)
{
	if (!settings || !out_config)
		return;

	memset(out_config, 0, sizeof(*out_config));
	out_config->enabled = obs_data_get_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED) ? 1 : 0;
	out_config->mode = (uint32_t)obs_data_get_int(settings, SETTING_INVERT_HUE_QUALIFIER_MODE);

	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		struct lenses_invert_hue_range_band *band = &out_config->bands[i];
		band->enabled = obs_data_get_bool(settings, k_band_enabled_keys[i]) ? 1 : 0;
		band->center_degrees = (float)obs_data_get_double(settings, k_band_center_keys[i]);
		band->width_degrees = (float)obs_data_get_double(settings, k_band_width_keys[i]);
		band->softness_degrees = (float)obs_data_get_double(settings, k_band_softness_keys[i]);
	}

	lenses_hue_qualifier_clamp(out_config);
}

void lenses_hue_qualifier_store_settings(obs_data_t *settings,
					 const struct lenses_invert_hue_range_config *config)
{
	if (!settings || !config)
		return;

	struct lenses_invert_hue_range_config clamped = *config;
	lenses_hue_qualifier_clamp(&clamped);

	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		const struct lenses_invert_hue_range_band *band = &clamped.bands[i];
		obs_data_set_bool(settings, k_band_enabled_keys[i], band->enabled);
		obs_data_set_double(settings, k_band_center_keys[i], band->center_degrees);
		obs_data_set_double(settings, k_band_width_keys[i], band->width_degrees);
		obs_data_set_double(settings, k_band_softness_keys[i], band->softness_degrees);
	}
}

static void set_band_defaults(obs_data_t *settings)
{
	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		const struct lenses_invert_hue_range_band band = default_band(i);
		if (!obs_data_has_user_value(settings, k_band_enabled_keys[i]))
			obs_data_set_bool(settings, k_band_enabled_keys[i], band.enabled != 0);
		if (!obs_data_has_user_value(settings, k_band_center_keys[i]))
			obs_data_set_double(settings, k_band_center_keys[i], band.center_degrees);
		if (!obs_data_has_user_value(settings, k_band_width_keys[i]))
			obs_data_set_double(settings, k_band_width_keys[i], band.width_degrees);
		if (!obs_data_has_user_value(settings, k_band_softness_keys[i]))
			obs_data_set_double(settings, k_band_softness_keys[i], band.softness_degrees);
	}
}

void lenses_hue_qualifier_migrate_from_legacy(obs_data_t *settings, float hue_min_degrees,
					      float hue_max_degrees)
{
	if (!settings)
		return;

	if (!obs_data_has_user_value(settings, SETTING_INVERT_HUE_QUALIFIER_MODE))
		obs_data_set_int(settings, SETTING_INVERT_HUE_QUALIFIER_MODE,
				 LENSES_INVERT_HUE_QUALIFIER_DEFAULT_MODE);
	set_band_defaults(settings);

	if (obs_data_has_user_value(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED))
		return;

	const float min_deg = normalize_degrees(hue_min_degrees);
	const float max_deg = normalize_degrees(hue_max_degrees);
	float width_deg = 0.0f;
	float center_deg = 0.0f;
	const bool wrap = min_deg > max_deg;
	if (!wrap) {
		width_deg = max_deg - min_deg;
		center_deg = min_deg + (width_deg * 0.5f);
	} else {
		width_deg = (360.0f - min_deg) + max_deg;
		center_deg = normalize_degrees(min_deg + (width_deg * 0.5f));
	}

	if (width_deg >= 359.5f || width_deg <= 0.0001f) {
		obs_data_set_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED, false);
		return;
	}

	obs_data_set_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED, true);
	obs_data_set_bool(settings, k_band_enabled_keys[0], true);
	obs_data_set_double(settings, k_band_center_keys[0], center_deg);
	obs_data_set_double(settings, k_band_width_keys[0], width_deg);
	obs_data_set_double(settings, k_band_softness_keys[0], 12.0);
}

uint32_t lenses_hue_qualifier_active_band_count(const struct lenses_invert_hue_range_config *config)
{
	if (!config || !config->enabled)
		return 0;

	uint32_t active = 0;
	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		const struct lenses_invert_hue_range_band *band = &config->bands[i];
		if (band->enabled && band->width_degrees > 0.0001f)
			active++;
	}
	return active;
}

void lenses_hue_qualifier_format_band_summary(const struct lenses_invert_hue_range_config *config,
					      char *out, size_t out_size)
{
	if (!out || out_size == 0)
		return;

	if (!config || !config->enabled) {
		snprintf(out, out_size, "%s", "disabled");
		return;
	}

	const uint32_t active = lenses_hue_qualifier_active_band_count(config);
	const char *mode = config->mode == LENSES_INVERT_HUE_RANGE_MODE_INCLUDE ? "include" : "exclude";
	snprintf(out, out_size, "%s active_bands=%" PRIu32, mode, active);
}
