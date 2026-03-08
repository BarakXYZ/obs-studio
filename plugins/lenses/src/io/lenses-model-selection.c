#include "lenses/io/lenses-model-catalog.h"

#include <string.h>

static char lenses_model_selection_normalize_size_tier(const char *size_tier)
{
	if (!size_tier || !*size_tier)
		return '\0';

	switch (size_tier[0]) {
	case 'n':
	case 'N':
		return 'n';
	case 's':
	case 'S':
		return 's';
	case 'm':
	case 'M':
		return 'm';
	case 'l':
	case 'L':
		return 'l';
	case 'x':
	case 'X':
		return 'x';
	default:
		return '\0';
	}
}

static int lenses_model_selection_tier_rank(char size_tier)
{
	switch (size_tier) {
	case 'n':
		return 0;
	case 's':
		return 1;
	case 'm':
		return 2;
	case 'l':
		return 3;
	case 'x':
		return 4;
	default:
		return -1;
	}
}

static char lenses_model_selection_tier_for_rank(int rank)
{
	static const char tiers[] = {'n', 's', 'm', 'l', 'x'};
	if (rank < 0 || rank >= (int)(sizeof(tiers) / sizeof(tiers[0])))
		return '\0';
	return tiers[rank];
}

static const struct lenses_model_catalog_entry *
lenses_model_selection_pick_exact(const struct lenses_model_catalog *catalog, char target,
				      bool prefer_built_in)
{
	const struct lenses_model_catalog_entry *best = NULL;

	for (size_t i = 0; i < catalog->count; ++i) {
		const struct lenses_model_catalog_entry *entry = &catalog->entries[i];
		if (lenses_model_selection_normalize_size_tier(entry->size_tier) != target)
			continue;

		if (!best) {
			best = entry;
			continue;
		}
		if (prefer_built_in && best->built_in != entry->built_in) {
			if (entry->built_in)
				best = entry;
			continue;
		}
		if (entry->class_count > best->class_count) {
			best = entry;
			continue;
		}
		if (strcmp(entry->name, best->name) < 0)
			best = entry;
	}

	return best;
}

const struct lenses_model_catalog_entry *
lenses_model_catalog_pick_by_size(const struct lenses_model_catalog *catalog, const char *size_tier,
				  bool prefer_built_in)
{
	if (!catalog || catalog->count == 0)
		return NULL;

	const char target = lenses_model_selection_normalize_size_tier(size_tier);
	const int target_rank = lenses_model_selection_tier_rank(target);
	if (target_rank < 0)
		return NULL;

	for (int rank = target_rank; rank >= 0; --rank) {
		const struct lenses_model_catalog_entry *best =
			lenses_model_selection_pick_exact(catalog,
						       lenses_model_selection_tier_for_rank(rank),
						       prefer_built_in);
		if (best)
			return best;
	}

	for (int rank = target_rank + 1; rank < 5; ++rank) {
		const struct lenses_model_catalog_entry *best =
			lenses_model_selection_pick_exact(catalog,
						       lenses_model_selection_tier_for_rank(rank),
						       prefer_built_in);
		if (best)
			return best;
	}

	return NULL;
}
