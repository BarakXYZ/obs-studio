#pragma once

#include "filter/invert/lenses-invert-hue-qualifier.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lenses_hue_preset_id {
	LENSES_HUE_PRESET_CUSTOM = -1,
	LENSES_HUE_PRESET_NATURAL_GUARDS = 0,
	LENSES_HUE_PRESET_SKY_GUARD = 1,
	LENSES_HUE_PRESET_FOLIAGE_GUARD = 2,
	LENSES_HUE_PRESET_SKIN_GUARD = 3,
};

struct lenses_hue_preset_definition {
	enum lenses_hue_preset_id id;
	const char *label_key;
	const char *description_key;
	uint8_t qualifier_enabled;
	uint32_t mode;
	struct lenses_invert_hue_range_band bands[LENSES_INVERT_HUE_RANGE_MAX_BANDS];
};

enum lenses_hue_band_template_id {
	LENSES_HUE_BAND_TEMPLATE_SKY = 0,
	LENSES_HUE_BAND_TEMPLATE_FOLIAGE = 1,
	LENSES_HUE_BAND_TEMPLATE_SKIN = 2,
};

struct lenses_hue_band_template {
	enum lenses_hue_band_template_id id;
	const char *label_key;
	const char *description_key;
	struct lenses_invert_hue_range_band band;
};

size_t lenses_hue_preset_count(void);
const struct lenses_hue_preset_definition *lenses_hue_preset_at(size_t index);
const struct lenses_hue_preset_definition *lenses_hue_preset_default(void);
bool lenses_hue_preset_apply(enum lenses_hue_preset_id id, bool *out_qualifier_enabled,
			     uint32_t *out_mode, struct lenses_invert_hue_range_config *out_config);
enum lenses_hue_preset_id lenses_hue_preset_detect(bool qualifier_enabled, uint32_t mode,
						    const struct lenses_invert_hue_range_config *config);

size_t lenses_hue_band_template_count(void);
const struct lenses_hue_band_template *lenses_hue_band_template_at(size_t index);

#ifdef __cplusplus
}
#endif
