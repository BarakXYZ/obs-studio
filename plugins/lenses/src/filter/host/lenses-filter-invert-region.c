#include "filter/host/lenses-filter-invert-region.h"

static float lenses_clampf(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

void lenses_invert_region_clamp(struct lenses_invert_region_config *config)
{
	if (!config)
		return;

	config->threshold = lenses_clampf(config->threshold, LENSES_INVERT_REGION_THRESHOLD_MIN,
					  LENSES_INVERT_REGION_THRESHOLD_MAX);
	config->softness = lenses_clampf(config->softness, LENSES_INVERT_REGION_SOFTNESS_MIN,
					 LENSES_INVERT_REGION_SOFTNESS_MAX);
	config->coverage = lenses_clampf(config->coverage, LENSES_INVERT_REGION_COVERAGE_MIN,
					 LENSES_INVERT_REGION_COVERAGE_MAX);
	config->luma_min = lenses_clampf(config->luma_min, LENSES_INVERT_REGION_LUMA_MIN_MIN,
					 LENSES_INVERT_REGION_LUMA_MIN_MAX);
	config->luma_max = lenses_clampf(config->luma_max, LENSES_INVERT_REGION_LUMA_MAX_MIN,
					 LENSES_INVERT_REGION_LUMA_MAX_MAX);
	if (config->luma_min > config->luma_max) {
		const float swapped = config->luma_min;
		config->luma_min = config->luma_max;
		config->luma_max = swapped;
	}
	config->saturation_min =
		lenses_clampf(config->saturation_min, LENSES_INVERT_REGION_SAT_MIN_MIN,
			      LENSES_INVERT_REGION_SAT_MIN_MAX);
	config->saturation_max =
		lenses_clampf(config->saturation_max, LENSES_INVERT_REGION_SAT_MAX_MIN,
			      LENSES_INVERT_REGION_SAT_MAX_MAX);
	if (config->saturation_min > config->saturation_max) {
		const float swapped = config->saturation_min;
		config->saturation_min = config->saturation_max;
		config->saturation_max = swapped;
	}
	config->hue_min_degrees =
		lenses_clampf(config->hue_min_degrees, LENSES_INVERT_REGION_HUE_MIN_DEGREES_MIN,
			      LENSES_INVERT_REGION_HUE_MIN_DEGREES_MAX);
	config->hue_max_degrees =
		lenses_clampf(config->hue_max_degrees, LENSES_INVERT_REGION_HUE_MAX_DEGREES_MIN,
			      LENSES_INVERT_REGION_HUE_MAX_DEGREES_MAX);
}

void lenses_invert_region_set_default_settings(obs_data_t *settings)
{
	if (!settings)
		return;

	obs_data_set_default_double(settings, SETTING_INVERT_REGION_THRESHOLD,
				    LENSES_INVERT_REGION_THRESHOLD_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_SOFTNESS,
				    LENSES_INVERT_REGION_SOFTNESS_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_COVERAGE,
				    LENSES_INVERT_REGION_COVERAGE_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_LUMA_MIN,
				    LENSES_INVERT_REGION_LUMA_MIN_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_LUMA_MAX,
				    LENSES_INVERT_REGION_LUMA_MAX_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_SAT_MIN,
				    LENSES_INVERT_REGION_SAT_MIN_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_SAT_MAX,
				    LENSES_INVERT_REGION_SAT_MAX_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_HUE_MIN_DEGREES,
				    LENSES_INVERT_REGION_HUE_MIN_DEGREES_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_REGION_HUE_MAX_DEGREES,
				    LENSES_INVERT_REGION_HUE_MAX_DEGREES_DEFAULT);
}

void lenses_invert_region_migrate_settings(obs_data_t *settings)
{
	if (!settings)
		return;

	if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_THRESHOLD) &&
	    obs_data_has_user_value(settings, SETTING_INVERT_REGION_THRESHOLD_LEGACY)) {
		obs_data_set_double(settings, SETTING_INVERT_REGION_THRESHOLD,
				    obs_data_get_double(settings, SETTING_INVERT_REGION_THRESHOLD_LEGACY));
	}
	if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_SOFTNESS) &&
	    obs_data_has_user_value(settings, SETTING_INVERT_REGION_SOFTNESS_LEGACY)) {
		obs_data_set_double(settings, SETTING_INVERT_REGION_SOFTNESS,
				    obs_data_get_double(settings, SETTING_INVERT_REGION_SOFTNESS_LEGACY));
	}
	if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_COVERAGE) &&
	    obs_data_has_user_value(settings, SETTING_INVERT_REGION_COVERAGE_LEGACY)) {
		obs_data_set_double(settings, SETTING_INVERT_REGION_COVERAGE,
				    obs_data_get_double(settings, SETTING_INVERT_REGION_COVERAGE_LEGACY));
	}
}

void lenses_invert_region_load_settings(obs_data_t *settings,
					 struct lenses_invert_region_config *out_config)
{
	if (!settings || !out_config)
		return;

	out_config->threshold = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_THRESHOLD);
	out_config->softness = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_SOFTNESS);
	out_config->coverage = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_COVERAGE);
	out_config->luma_min = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_LUMA_MIN);
	out_config->luma_max = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_LUMA_MAX);
	out_config->saturation_min = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_SAT_MIN);
	out_config->saturation_max = (float)obs_data_get_double(settings, SETTING_INVERT_REGION_SAT_MAX);
	out_config->hue_min_degrees =
		(float)obs_data_get_double(settings, SETTING_INVERT_REGION_HUE_MIN_DEGREES);
	out_config->hue_max_degrees =
		(float)obs_data_get_double(settings, SETTING_INVERT_REGION_HUE_MAX_DEGREES);
	lenses_invert_region_clamp(out_config);
}
