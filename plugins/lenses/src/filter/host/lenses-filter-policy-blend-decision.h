#pragma once

#include <stdbool.h>

struct lenses_rule_blend_decision {
	bool skip_rule;
	bool use_mask;
	float region_mode;
};

static inline struct lenses_rule_blend_decision
lenses_policy_decide_rule_blend(bool targets_masks, bool has_rule_mask, int region_mode)
{
	struct lenses_rule_blend_decision decision = {
		.skip_rule = false,
		.use_mask = false,
		.region_mode = region_mode == 1 ? 1.0f : 0.0f,
	};

	if (!targets_masks)
		return decision;
	if (has_rule_mask) {
		decision.use_mask = true;
		return decision;
	}
	if (region_mode == 0) {
		decision.skip_rule = true;
		return decision;
	}

	/*
	 * Exclude mode with no selector mask means "exclude nothing", so apply
	 * the overlay globally instead of producing a no-op.
	 */
	decision.region_mode = 0.0f;
	return decision;
}
