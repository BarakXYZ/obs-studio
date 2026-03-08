#pragma once

#include <obs-data.h>

#define SETTING_INVERT_REGION_THRESHOLD "invert_region_threshold"
#define SETTING_INVERT_REGION_SOFTNESS "invert_region_softness"
#define SETTING_INVERT_REGION_COVERAGE "invert_region_coverage"
#define SETTING_INVERT_REGION_LUMA_MIN "invert_region_luma_min"
#define SETTING_INVERT_REGION_LUMA_MAX "invert_region_luma_max"
#define SETTING_INVERT_REGION_SAT_MIN "invert_region_sat_min"
#define SETTING_INVERT_REGION_SAT_MAX "invert_region_sat_max"
#define SETTING_INVERT_REGION_HUE_MIN_DEGREES "invert_region_hue_min_degrees"
#define SETTING_INVERT_REGION_HUE_MAX_DEGREES "invert_region_hue_max_degrees"

#define SETTING_INVERT_REGION_THRESHOLD_LEGACY "white_bias"
#define SETTING_INVERT_REGION_SOFTNESS_LEGACY "white_softness"
#define SETTING_INVERT_REGION_COVERAGE_LEGACY "white_region_floor"

#define LENSES_INVERT_REGION_THRESHOLD_DEFAULT 0.88f
#define LENSES_INVERT_REGION_SOFTNESS_DEFAULT 0.16f
#define LENSES_INVERT_REGION_COVERAGE_DEFAULT 0.70f
#define LENSES_INVERT_REGION_LUMA_MIN_DEFAULT 0.77f
#define LENSES_INVERT_REGION_LUMA_MAX_DEFAULT 1.0f
#define LENSES_INVERT_REGION_SAT_MIN_DEFAULT 0.0f
#define LENSES_INVERT_REGION_SAT_MAX_DEFAULT 0.21f
#define LENSES_INVERT_REGION_HUE_MIN_DEGREES_DEFAULT 0.0f
#define LENSES_INVERT_REGION_HUE_MAX_DEGREES_DEFAULT 360.0f

#define LENSES_INVERT_REGION_THRESHOLD_MIN 0.0f
#define LENSES_INVERT_REGION_THRESHOLD_MAX 1.0f
#define LENSES_INVERT_REGION_SOFTNESS_MIN 0.0f
#define LENSES_INVERT_REGION_SOFTNESS_MAX 1.0f
#define LENSES_INVERT_REGION_COVERAGE_MIN 0.0f
#define LENSES_INVERT_REGION_COVERAGE_MAX 1.0f
#define LENSES_INVERT_REGION_LUMA_MIN_MIN 0.0f
#define LENSES_INVERT_REGION_LUMA_MIN_MAX 1.0f
#define LENSES_INVERT_REGION_LUMA_MAX_MIN 0.0f
#define LENSES_INVERT_REGION_LUMA_MAX_MAX 1.0f
#define LENSES_INVERT_REGION_SAT_MIN_MIN 0.0f
#define LENSES_INVERT_REGION_SAT_MIN_MAX 1.0f
#define LENSES_INVERT_REGION_SAT_MAX_MIN 0.0f
#define LENSES_INVERT_REGION_SAT_MAX_MAX 1.0f
#define LENSES_INVERT_REGION_HUE_MIN_DEGREES_MIN 0.0f
#define LENSES_INVERT_REGION_HUE_MIN_DEGREES_MAX 360.0f
#define LENSES_INVERT_REGION_HUE_MAX_DEGREES_MIN 0.0f
#define LENSES_INVERT_REGION_HUE_MAX_DEGREES_MAX 360.0f

#define LENSES_INVERT_REGION_SLIDER_STEP 0.01f
#define LENSES_INVERT_REGION_HUE_SLIDER_STEP 1.0f

struct lenses_invert_region_config {
	float threshold;
	float softness;
	float coverage;
	float luma_min;
	float luma_max;
	float saturation_min;
	float saturation_max;
	float hue_min_degrees;
	float hue_max_degrees;
};

void lenses_invert_region_set_default_settings(obs_data_t *settings);
void lenses_invert_region_migrate_settings(obs_data_t *settings);
void lenses_invert_region_load_settings(obs_data_t *settings,
					 struct lenses_invert_region_config *out_config);
void lenses_invert_region_clamp(struct lenses_invert_region_config *config);
