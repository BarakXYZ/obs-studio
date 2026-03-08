#include <obs-module.h>

#include "lenses/lenses-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("lenses", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Lenses realtime visual enhancement filters";
}

bool obs_module_load(void)
{
	obs_register_source(&lenses_filter_source);
	blog(LOG_INFO, "[lenses] plugin loaded");
	return true;
}
