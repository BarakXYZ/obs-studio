#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LENSES_POLICY_STATUS_MAX 256
#define LENSES_POLICY_MAX_RULES 32
#define LENSES_POLICY_ID_MAX 64
#define LENSES_POLICY_CHAIN_MAX 32
#define LENSES_POLICY_BLEND_MAX 16
#define LENSES_POLICY_MAX_SELECTOR_CLASS_IDS 32

struct lenses_policy_compile_result {
	bool valid;
	size_t rule_count;
	uint64_t deterministic_hash;
	char message[LENSES_POLICY_STATUS_MAX];
};

struct lenses_policy_rule_runtime {
	char id[LENSES_POLICY_ID_MAX];
	int priority;
	int class_id;
	size_t class_id_count;
	int class_ids[LENSES_POLICY_MAX_SELECTOR_CLASS_IDS];
	int region_mode;
	char filter_chain[LENSES_POLICY_CHAIN_MAX];
	char blend_mode[LENSES_POLICY_BLEND_MAX];
	float opacity;
};

struct lenses_policy_runtime {
	size_t rule_count;
	struct lenses_policy_rule_runtime rules[LENSES_POLICY_MAX_RULES];
	char default_filter_chain[LENSES_POLICY_CHAIN_MAX];
};

bool lenses_policy_compile_file(const char *path, struct lenses_policy_compile_result *out_result);

bool lenses_policy_apply_legacy_overrides_from_file(const char *path, bool *invert_enabled,
						    float *invert_strength,
						    float *invert_region_threshold,
						    float *invert_region_softness,
						    float *invert_region_coverage,
						    struct lenses_policy_compile_result *out_result);

bool lenses_policy_load_runtime_file(const char *path, struct lenses_policy_runtime *out_policy,
				     struct lenses_policy_compile_result *out_result);

bool lenses_policy_write_legacy_preset(const char *path, const char *preset_id, bool invert_enabled,
				       float invert_strength,
				       float invert_region_threshold,
				       float invert_region_softness,
				       float invert_region_coverage,
				       struct lenses_policy_compile_result *out_result);

#ifdef __cplusplus
}
#endif
