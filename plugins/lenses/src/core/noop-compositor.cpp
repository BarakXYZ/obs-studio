#include "lenses/core/noop-compositor.hpp"

namespace lenses::core {

ComposeResult NoopCompositor::Compose(const ComposeRequest &request, const ExecutionPlan &plan,
				      const MaskFrame *mask_frame)
{
	(void)request;
	(void)mask_frame;

	ComposeResult result{};
	result.applied_rule_ids.reserve(plan.ordered_rules.size());
	for (const auto &rule : plan.ordered_rules)
		result.applied_rule_ids.push_back(rule.id);

	return result;
}

} // namespace lenses::core
