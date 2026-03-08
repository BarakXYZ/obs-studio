#pragma once

#include "lenses/core/interfaces.hpp"

namespace lenses::core {

class DeterministicRuleCompiler final : public IRuleCompiler {
public:
	ExecutionPlan Compile(const std::vector<Rule> &rules,
			      const std::optional<Rule> &default_rule) override;
};

} // namespace lenses::core
