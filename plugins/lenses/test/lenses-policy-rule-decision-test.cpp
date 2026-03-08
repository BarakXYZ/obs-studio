#include "filter/host/lenses-filter-policy-blend-decision.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const std::string &message)
{
	if (condition)
		return true;
	std::cerr << "FAIL: " << message << std::endl;
	return false;
}

bool TestGlobalRuleHasNoMaskAndNoSkip()
{
	const auto decision = lenses_policy_decide_rule_blend(false, false, 0);
	if (!Expect(!decision.skip_rule, "global rule should not be skipped"))
		return false;
	if (!Expect(!decision.use_mask, "global rule should not require a mask"))
		return false;
	return Expect(decision.region_mode == 0.0f, "global include rule should keep include mode");
}

bool TestIncludeSelectorWithoutMaskSkipsRule()
{
	const auto decision = lenses_policy_decide_rule_blend(true, false, 0);
	if (!Expect(decision.skip_rule, "include selector with no mask should skip rule"))
		return false;
	if (!Expect(!decision.use_mask, "skipped include rule should not use mask"))
		return false;
	return Expect(decision.region_mode == 0.0f, "include selector should keep include mode");
}

bool TestExcludeSelectorWithoutMaskFallsBackToGlobalApply()
{
	const auto decision = lenses_policy_decide_rule_blend(true, false, 1);
	if (!Expect(!decision.skip_rule, "exclude selector with no mask should still apply"))
		return false;
	if (!Expect(!decision.use_mask, "exclude selector without mask should not sample mask"))
		return false;
	return Expect(decision.region_mode == 0.0f,
		      "exclude selector with no mask should apply globally");
}

bool TestExcludeSelectorWithMaskUsesMask()
{
	const auto decision = lenses_policy_decide_rule_blend(true, true, 1);
	if (!Expect(!decision.skip_rule, "exclude selector with mask should apply"))
		return false;
	if (!Expect(decision.use_mask, "exclude selector with mask should use mask"))
		return false;
	return Expect(decision.region_mode == 1.0f,
		      "exclude selector with mask should preserve exclude mode");
}

} // namespace

int main()
{
	bool ok = true;
	ok = TestGlobalRuleHasNoMaskAndNoSkip() && ok;
	ok = TestIncludeSelectorWithoutMaskSkipsRule() && ok;
	ok = TestExcludeSelectorWithoutMaskFallsBackToGlobalApply() && ok;
	ok = TestExcludeSelectorWithMaskUsesMask() && ok;
	if (!ok)
		return 1;

	std::cout << "lenses-policy-rule-decision-test: PASS" << std::endl;
	return 0;
}
