#pragma once

#include "filter/invert/lenses-invert-hue-qualifier.h"

#include <obs-data.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTING_INVERT_HUE_QUALIFIER_ENABLED "invert_hue_qualifier_enabled"
#define SETTING_INVERT_HUE_QUALIFIER_MODE "invert_hue_qualifier_mode"

#define LENSES_INVERT_HUE_QUALIFIER_DEFAULT_ENABLED false
#define LENSES_INVERT_HUE_QUALIFIER_DEFAULT_MODE LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE

#define LENSES_INVERT_HUE_RANGE_CENTER_DEFAULT 0.0f
#define LENSES_INVERT_HUE_RANGE_WIDTH_DEFAULT 24.0f
#define LENSES_INVERT_HUE_RANGE_SOFTNESS_DEFAULT 12.0f

#define LENSES_INVERT_HUE_RANGE_CENTER_MIN 0.0f
#define LENSES_INVERT_HUE_RANGE_CENTER_MAX 360.0f
#define LENSES_INVERT_HUE_RANGE_WIDTH_MIN 0.0f
#define LENSES_INVERT_HUE_RANGE_WIDTH_MAX 360.0f
#define LENSES_INVERT_HUE_RANGE_SOFTNESS_MIN 0.0f
#define LENSES_INVERT_HUE_RANGE_SOFTNESS_MAX 180.0f

enum lenses_hue_band_setting_field {
	LENSES_HUE_BAND_SETTING_ENABLED = 0,
	LENSES_HUE_BAND_SETTING_CENTER_DEGREES = 1,
	LENSES_HUE_BAND_SETTING_WIDTH_DEGREES = 2,
	LENSES_HUE_BAND_SETTING_SOFTNESS_DEGREES = 3,
};

const char *lenses_hue_band_setting_key(enum lenses_hue_band_setting_field field, uint32_t band_index);
void lenses_hue_qualifier_set_default_settings(obs_data_t *settings);
void lenses_hue_qualifier_clamp(struct lenses_invert_hue_range_config *config);
void lenses_hue_qualifier_load_settings(obs_data_t *settings,
					struct lenses_invert_hue_range_config *out_config);
void lenses_hue_qualifier_store_settings(obs_data_t *settings,
					 const struct lenses_invert_hue_range_config *config);
void lenses_hue_qualifier_migrate_from_legacy(obs_data_t *settings, float hue_min_degrees,
					      float hue_max_degrees);
uint32_t lenses_hue_qualifier_active_band_count(const struct lenses_invert_hue_range_config *config);
void lenses_hue_qualifier_format_band_summary(const struct lenses_invert_hue_range_config *config,
					      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
