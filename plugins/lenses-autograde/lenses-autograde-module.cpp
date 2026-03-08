#include <obs-module.h>

#include "lenses-autograde/autograde-filter.hpp"
#include "lenses-autograde/frontend-controller.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("lenses-autograde", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Standalone deterministic autograde filter with ROI selection";
}

bool obs_module_load(void)
{
	obs_register_source(&lenses_autograde_filter_source);
	lenses_autograde::frontend::Initialize();
	blog(LOG_INFO, "[lenses-autograde] plugin loaded");
	return true;
}

void obs_module_unload(void)
{
	lenses_autograde::frontend::Shutdown();
	blog(LOG_INFO, "[lenses-autograde] plugin unloaded");
}
