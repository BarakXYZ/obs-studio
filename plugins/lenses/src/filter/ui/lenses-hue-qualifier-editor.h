#pragma once

#include <stdbool.h>

struct lenses_filter_data;

#ifdef __cplusplus
extern "C" {
#endif

bool lenses_hue_qualifier_open_editor(struct lenses_filter_data *filter);

#ifdef __cplusplus
}
#endif
