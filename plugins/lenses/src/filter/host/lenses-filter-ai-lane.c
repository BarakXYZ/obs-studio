#include "filter/host/lenses-filter-internal.h"

bool lenses_rule_targets_class_masks(const struct lenses_policy_rule_runtime *rule)
{
	if (!rule)
		return false;

	return rule->class_id_count > 0 || rule->class_id >= 0;
}

bool lenses_filter_policy_requires_ai_masks(const struct lenses_filter_data *filter)
{
	if (!filter || !filter->policy_runtime_valid)
		return false;

	for (size_t i = 0; i < filter->policy_runtime.rule_count; ++i) {
		if (lenses_rule_targets_class_masks(&filter->policy_runtime.rules[i]))
			return true;
	}

	return false;
}

bool lenses_filter_debug_requires_ai_masks(const struct lenses_filter_data *filter)
{
	if (!filter)
		return false;

	return filter->debug_enabled && filter->debug_mask_overlay &&
	       filter->debug_overlay_mode == LENSES_DEBUG_OVERLAY_MODE_SEGMENTS;
}

bool lenses_filter_needs_ai_masks(const struct lenses_filter_data *filter)
{
	if (!filter)
		return false;

	return lenses_filter_policy_requires_ai_masks(filter) ||
	       lenses_filter_debug_requires_ai_masks(filter);
}

bool lenses_filter_ai_lane_active(const struct lenses_filter_data *filter)
{
	if (!filter || !filter->core || !filter->ai_enabled || filter->ai_target_fps == 0)
		return false;

	return lenses_filter_needs_ai_masks(filter);
}
