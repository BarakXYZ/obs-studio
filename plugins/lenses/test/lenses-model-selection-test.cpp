#include "lenses/io/lenses-model-catalog.h"

#include <cstdio>
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

void SetEntry(struct lenses_model_catalog_entry &entry, const char *id, const char *name,
	      const char *size_tier, bool built_in, uint32_t class_count)
{
	snprintf(entry.id, sizeof(entry.id), "%s", id);
	snprintf(entry.name, sizeof(entry.name), "%s", name);
	snprintf(entry.size_tier, sizeof(entry.size_tier), "%s", size_tier);
	entry.built_in = built_in;
	entry.class_count = class_count;
}

bool TestExactTierPrefersBuiltIn()
{
	struct lenses_model_catalog catalog = {};
	catalog.count = 2;
	SetEntry(catalog.entries[0], "user-m", "User Medium", "m", false, 80);
	SetEntry(catalog.entries[1], "builtin-m", "Built In Medium", "m", true, 80);

	const auto *selected = lenses_model_catalog_pick_by_size(&catalog, "m", true);
	if (!Expect(selected != nullptr, "expected an exact medium-tier selection"))
		return false;
	return Expect(std::string(selected->id) == "builtin-m",
		      "expected built-in medium model to win exact-tier tie-break");
}

bool TestFallsBackToNearestLowerTier()
{
	struct lenses_model_catalog catalog = {};
	catalog.count = 3;
	SetEntry(catalog.entries[0], "builtin-s", "Built In Small", "s", true, 80);
	SetEntry(catalog.entries[1], "builtin-m", "Built In Medium", "m", true, 80);
	SetEntry(catalog.entries[2], "user-x", "User XLarge", "x", false, 80);

	const auto *selected = lenses_model_catalog_pick_by_size(&catalog, "l", true);
	if (!Expect(selected != nullptr, "expected a fallback selection for missing large tier"))
		return false;
	return Expect(std::string(selected->id) == "builtin-m",
		      "expected missing large tier to fall back to nearest lower installed tier");
}

bool TestFallsBackUpwardWhenNoLowerTierExists()
{
	struct lenses_model_catalog catalog = {};
	catalog.count = 1;
	SetEntry(catalog.entries[0], "builtin-s", "Built In Small", "s", true, 80);

	const auto *selected = lenses_model_catalog_pick_by_size(&catalog, "n", true);
	if (!Expect(selected != nullptr, "expected an upward fallback when no lower tier exists"))
		return false;
	return Expect(std::string(selected->id) == "builtin-s",
		      "expected nano request to use the nearest larger installed tier");
}

bool TestRejectsUnknownTier()
{
	struct lenses_model_catalog catalog = {};
	catalog.count = 1;
	SetEntry(catalog.entries[0], "builtin-s", "Built In Small", "s", true, 80);

	return Expect(lenses_model_catalog_pick_by_size(&catalog, "auto", true) == nullptr,
		      "expected unknown size tiers to return no selection");
}

} // namespace

int main()
{
	bool ok = true;
	ok = TestExactTierPrefersBuiltIn() && ok;
	ok = TestFallsBackToNearestLowerTier() && ok;
	ok = TestFallsBackUpwardWhenNoLowerTierExists() && ok;
	ok = TestRejectsUnknownTier() && ok;
	if (!ok)
		return 1;

	std::cout << "lenses-model-selection-test: PASS" << std::endl;
	return 0;
}
