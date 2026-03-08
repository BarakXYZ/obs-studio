#include "lenses/core/rule-compiler.hpp"

#include <algorithm>

namespace lenses::core {

ExecutionPlan DeterministicRuleCompiler::Compile(const std::vector<Rule> &rules,
						 const std::optional<Rule> &default_rule)
{
	ExecutionPlan plan{};
	plan.default_rule = default_rule;

	plan.ordered_rules.reserve(rules.size());
	for (const auto &rule : rules) {
		if (!rule.enabled)
			continue;
		plan.ordered_rules.push_back(rule);
	}

	std::sort(plan.ordered_rules.begin(), plan.ordered_rules.end(), [](const Rule &lhs, const Rule &rhs) {
		if (lhs.priority != rhs.priority)
			return lhs.priority > rhs.priority;
		return lhs.id < rhs.id;
	});

	plan.revision++;
	return plan;
}

} // namespace lenses::core
