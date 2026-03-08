#include "lenses/io/lenses-policy.h"

#include <obs-data.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <stdlib.h>
#define LENSES_STRCASECMP _stricmp
#else
#include <strings.h>
#define LENSES_STRCASECMP strcasecmp
#endif

#define LENSES_POLICY_VERSION 1

struct compiled_rule {
	int priority;
	char *id;
};

static void lenses_policy_set_result(struct lenses_policy_compile_result *result, bool valid, size_t rule_count,
				     uint64_t deterministic_hash, const char *message)
{
	if (!result)
		return;

	result->valid = valid;
	result->rule_count = rule_count;
	result->deterministic_hash = deterministic_hash;

	if (!message)
		message = "";

	snprintf(result->message, sizeof(result->message), "%s", message);
}

static uint64_t lenses_hash_update(uint64_t hash, const void *bytes, size_t size)
{
	const unsigned char *cursor = bytes;
	for (size_t i = 0; i < size; i++) {
		hash ^= (uint64_t)cursor[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

static uint64_t lenses_hash_update_str(uint64_t hash, const char *text)
{
	if (!text)
		return hash;
	return lenses_hash_update(hash, text, strlen(text));
}

static int compiled_rule_compare(const void *lhs_ptr, const void *rhs_ptr)
{
	const struct compiled_rule *lhs = lhs_ptr;
	const struct compiled_rule *rhs = rhs_ptr;

	if (lhs->priority != rhs->priority)
		return rhs->priority - lhs->priority;
	return strcmp(lhs->id, rhs->id);
}

static int runtime_rule_compare(const void *lhs_ptr, const void *rhs_ptr)
{
	const struct lenses_policy_rule_runtime *lhs = lhs_ptr;
	const struct lenses_policy_rule_runtime *rhs = rhs_ptr;

	if (lhs->priority != rhs->priority)
		return rhs->priority - lhs->priority;
	return strcmp(lhs->id, rhs->id);
}

static void compiled_rule_clear(struct compiled_rule *rule)
{
	if (!rule)
		return;

	bfree(rule->id);
	rule->id = NULL;
	rule->priority = 0;
}

static float clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

static bool ensure_parent_directory(const char *path)
{
	if (!path || !*path)
		return false;

	struct dstr dir = {0};
	dstr_copy(&dir, path);
	char *slash = strrchr(dir.array, '/');
#ifdef _WIN32
	char *backslash = strrchr(dir.array, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
#endif
	if (!slash) {
		dstr_free(&dir);
		return true;
	}

	*slash = '\0';
	const bool ok = os_mkdirs(dir.array) == MKDIR_SUCCESS || os_file_exists(dir.array);
	dstr_free(&dir);
	return ok;
}

static bool compile_from_root(obs_data_t *root, struct lenses_policy_compile_result *out_result)
{
	if (!root) {
		lenses_policy_set_result(out_result, false, 0, 0, "Policy JSON root is null");
		return false;
	}

	const int version = (int)obs_data_get_int(root, "version");
	if (version != LENSES_POLICY_VERSION) {
		char message[LENSES_POLICY_STATUS_MAX] = {0};
		snprintf(message, sizeof(message), "Unsupported policy version: %d", version);
		lenses_policy_set_result(out_result, false, 0, 0, message);
		return false;
	}

	obs_data_array_t *rules = obs_data_get_array(root, "rules");
	if (!rules) {
		lenses_policy_set_result(out_result, false, 0, 0, "Policy missing required 'rules' array");
		return false;
	}

	const size_t raw_rule_count = obs_data_array_count(rules);
	struct compiled_rule *compiled_rules = bzalloc(sizeof(*compiled_rules) * (raw_rule_count + 1));
	size_t compiled_count = 0;

	for (size_t i = 0; i < raw_rule_count; i++) {
		obs_data_t *rule = obs_data_array_item(rules, i);
		if (!rule)
			continue;

		const bool has_enabled = obs_data_has_user_value(rule, "enabled");
		const bool enabled = has_enabled ? obs_data_get_bool(rule, "enabled") : true;
		if (!enabled) {
			obs_data_release(rule);
			continue;
		}

		const char *id = obs_data_get_string(rule, "id");
		char fallback_id[64] = {0};
		if (!id || !*id) {
			snprintf(fallback_id, sizeof(fallback_id), "rule_%zu", i);
			id = fallback_id;
		}

		compiled_rules[compiled_count].id = bstrdup(id);
		compiled_rules[compiled_count].priority =
			obs_data_has_user_value(rule, "priority") ? (int)obs_data_get_int(rule, "priority") : 0;
		compiled_count++;

		obs_data_release(rule);
	}

	qsort(compiled_rules, compiled_count, sizeof(*compiled_rules), compiled_rule_compare);

	uint64_t hash = 1469598103934665603ULL;
	for (size_t i = 0; i < compiled_count; i++) {
		hash = lenses_hash_update_str(hash, compiled_rules[i].id);
		hash = lenses_hash_update(hash, &compiled_rules[i].priority, sizeof(compiled_rules[i].priority));
	}

	obs_data_t *default_rule = obs_data_get_obj(root, "default_rule");
	if (default_rule) {
		const char *default_id = obs_data_get_string(default_rule, "id");
		hash = lenses_hash_update_str(hash, default_id);
		obs_data_release(default_rule);
	}

	char message[LENSES_POLICY_STATUS_MAX] = {0};
	snprintf(message, sizeof(message), "Policy compiled successfully (%zu active rules)", compiled_count);
	lenses_policy_set_result(out_result, true, compiled_count, hash, message);

	for (size_t i = 0; i < compiled_count; i++)
		compiled_rule_clear(&compiled_rules[i]);
	bfree(compiled_rules);
	obs_data_array_release(rules);
	return true;
}

static bool lenses_append_unique_class_id(int *class_ids, size_t *class_id_count,
					  size_t class_id_capacity, int class_id)
{
	if (!class_ids || !class_id_count || class_id_capacity == 0 || class_id < 0)
		return false;

	for (size_t i = 0; i < *class_id_count; ++i) {
		if (class_ids[i] == class_id)
			return true;
	}

	if (*class_id_count >= class_id_capacity)
		return false;

	class_ids[*class_id_count] = class_id;
	(*class_id_count)++;
	return true;
}

static bool lenses_extract_array_item_int(obs_data_t *item, int *out_value)
{
	if (!item || !out_value)
		return false;

	if (obs_data_has_user_value(item, "value")) {
		*out_value = (int)obs_data_get_int(item, "value");
		return true;
	}
	if (obs_data_has_user_value(item, "id")) {
		*out_value = (int)obs_data_get_int(item, "id");
		return true;
	}
	return false;
}

static const char *lenses_extract_array_item_string(obs_data_t *item)
{
	if (!item)
		return NULL;
	if (obs_data_has_user_value(item, "value"))
		return obs_data_get_string(item, "value");
	if (obs_data_has_user_value(item, "name"))
		return obs_data_get_string(item, "name");
	return NULL;
}

static int lenses_coco_class_id_from_name(const char *name)
{
	static const char *const kCocoClassNames[] = {
		"person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
		"truck", "boat", "traffic light", "fire hydrant", "stop sign", "parking meter",
		"bench", "bird", "cat", "dog", "horse", "sheep", "cow", "elephant",
		"bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie",
		"suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
		"baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
		"bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana",
		"apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza",
		"donut", "cake", "chair", "couch", "potted plant", "bed", "dining table",
		"toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
		"microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock",
		"vase", "scissors", "teddy bear", "hair drier", "toothbrush",
	};
	const size_t class_count = sizeof(kCocoClassNames) / sizeof(kCocoClassNames[0]);
	if (!name || !*name)
		return -1;

	for (size_t i = 0; i < class_count; ++i) {
		if (LENSES_STRCASECMP(name, kCocoClassNames[i]) == 0)
			return (int)i;
	}
	return -1;
}

static size_t collect_selector_class_ids(obs_data_t *rule, int *out_class_ids, size_t class_id_capacity)
{
	if (!rule || !out_class_ids || class_id_capacity == 0)
		return 0;

	size_t class_id_count = 0;
	obs_data_t *selector = obs_data_get_obj(rule, "selector");
	if (!selector)
		return 0;

	obs_data_array_t *class_ids = obs_data_get_array(selector, "class_ids");
	if (class_ids) {
		const size_t count = obs_data_array_count(class_ids);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(class_ids, i);
			int parsed = -1;
			if (lenses_extract_array_item_int(item, &parsed))
				(void)lenses_append_unique_class_id(out_class_ids, &class_id_count,
								    class_id_capacity, parsed);
			if (item)
				obs_data_release(item);
		}
		obs_data_array_release(class_ids);
	}

	obs_data_array_t *class_names = obs_data_get_array(selector, "class_names");
	if (class_names) {
		const size_t count = obs_data_array_count(class_names);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(class_names, i);
			const char *class_name = lenses_extract_array_item_string(item);
			const int mapped_id = lenses_coco_class_id_from_name(class_name);
			if (mapped_id >= 0)
				(void)lenses_append_unique_class_id(out_class_ids, &class_id_count,
								    class_id_capacity, mapped_id);
			if (item)
				obs_data_release(item);
		}
		obs_data_array_release(class_names);
	}

	obs_data_release(selector);
	return class_id_count;
}

static bool load_runtime_from_root(obs_data_t *root, struct lenses_policy_runtime *out_policy,
				   struct lenses_policy_compile_result *out_result)
{
	if (!out_policy) {
		lenses_policy_set_result(out_result, false, 0, 0, "Output runtime policy pointer is null");
		return false;
	}

	memset(out_policy, 0, sizeof(*out_policy));
	snprintf(out_policy->default_filter_chain, sizeof(out_policy->default_filter_chain), "%s",
		 "passthrough");

	if (!compile_from_root(root, out_result))
		return false;

	obs_data_array_t *rules = obs_data_get_array(root, "rules");
	if (!rules)
		return true;

	const size_t raw_count = obs_data_array_count(rules);
	size_t runtime_count = 0;
	for (size_t i = 0; i < raw_count && runtime_count < LENSES_POLICY_MAX_RULES; i++) {
		obs_data_t *rule = obs_data_array_item(rules, i);
		if (!rule)
			continue;

		const bool has_enabled = obs_data_has_user_value(rule, "enabled");
		const bool enabled = has_enabled ? obs_data_get_bool(rule, "enabled") : true;
		if (!enabled) {
			obs_data_release(rule);
			continue;
		}

		struct lenses_policy_rule_runtime *runtime_rule = &out_policy->rules[runtime_count];
		const char *id = obs_data_get_string(rule, "id");
		const char *filter_chain = obs_data_get_string(rule, "filter_chain");
		const char *blend_mode = obs_data_get_string(rule, "blend_mode");
		const char *region_mode = obs_data_get_string(rule, "region_mode");

		snprintf(runtime_rule->id, sizeof(runtime_rule->id), "%s", (id && *id) ? id : "rule");
		runtime_rule->priority =
			obs_data_has_user_value(rule, "priority") ? (int)obs_data_get_int(rule, "priority") : 0;
		runtime_rule->class_id_count = collect_selector_class_ids(
			rule, runtime_rule->class_ids, LENSES_POLICY_MAX_SELECTOR_CLASS_IDS);
		runtime_rule->class_id =
			(runtime_rule->class_id_count > 0) ? runtime_rule->class_ids[0] : -1;
		runtime_rule->region_mode =
			(region_mode && strcmp(region_mode, "exclude") == 0) ? 1 : 0;
		snprintf(runtime_rule->filter_chain, sizeof(runtime_rule->filter_chain), "%s",
			 (filter_chain && *filter_chain) ? filter_chain : "passthrough");
		snprintf(runtime_rule->blend_mode, sizeof(runtime_rule->blend_mode), "%s",
			 (blend_mode && *blend_mode) ? blend_mode : "replace");
		runtime_rule->opacity = clamp01((float)obs_data_get_double(rule, "opacity"));

		runtime_count++;
		obs_data_release(rule);
	}

	obs_data_t *default_rule = obs_data_get_obj(root, "default_rule");
	if (default_rule) {
		const char *default_chain = obs_data_get_string(default_rule, "filter_chain");
		if (default_chain && *default_chain) {
			snprintf(out_policy->default_filter_chain, sizeof(out_policy->default_filter_chain), "%s",
				 default_chain);
		}
		obs_data_release(default_rule);
	}

	qsort(out_policy->rules, runtime_count, sizeof(out_policy->rules[0]), runtime_rule_compare);
	out_policy->rule_count = runtime_count;
	obs_data_array_release(rules);
	return true;
}

bool lenses_policy_compile_file(const char *path, struct lenses_policy_compile_result *out_result)
{
	if (!path || !*path) {
		lenses_policy_set_result(out_result, false, 0, 0, "Policy path is empty");
		return false;
	}

	obs_data_t *root = obs_data_create_from_json_file(path);
	if (!root) {
		lenses_policy_set_result(out_result, false, 0, 0, "Failed to parse policy JSON file");
		return false;
	}

	const bool ok = compile_from_root(root, out_result);
	obs_data_release(root);
	return ok;
}

bool lenses_policy_apply_legacy_overrides_from_file(const char *path, bool *invert_enabled,
						    float *invert_strength,
						    float *invert_region_threshold,
						    float *invert_region_softness,
						    float *invert_region_coverage,
						    struct lenses_policy_compile_result *out_result)
{
	if (!path || !*path) {
		lenses_policy_set_result(out_result, false, 0, 0, "Policy path is empty");
		return false;
	}

	obs_data_t *root = obs_data_create_from_json_file(path);
	if (!root) {
		lenses_policy_set_result(out_result, false, 0, 0, "Failed to parse policy JSON file");
		return false;
	}

	const bool compiled = compile_from_root(root, out_result);
	if (!compiled) {
		obs_data_release(root);
		return false;
	}

	obs_data_t *legacy = obs_data_get_obj(root, "legacy");
	if (legacy) {
		if (invert_enabled && obs_data_has_user_value(legacy, "invert_enabled"))
			*invert_enabled = obs_data_get_bool(legacy, "invert_enabled");
		if (invert_strength && obs_data_has_user_value(legacy, "invert_strength"))
			*invert_strength = clamp01((float)obs_data_get_double(legacy, "invert_strength"));
		if (invert_region_threshold) {
			if (obs_data_has_user_value(legacy, "invert_region_threshold")) {
				*invert_region_threshold =
					clamp01((float)obs_data_get_double(legacy, "invert_region_threshold"));
			} else if (obs_data_has_user_value(legacy, "white_bias")) {
				*invert_region_threshold =
					clamp01((float)obs_data_get_double(legacy, "white_bias"));
			}
		}
		if (invert_region_softness) {
			if (obs_data_has_user_value(legacy, "invert_region_softness")) {
				*invert_region_softness =
					clamp01((float)obs_data_get_double(legacy, "invert_region_softness"));
			} else if (obs_data_has_user_value(legacy, "white_softness")) {
				*invert_region_softness =
					clamp01((float)obs_data_get_double(legacy, "white_softness"));
			}
		}
		if (invert_region_coverage) {
			if (obs_data_has_user_value(legacy, "invert_region_coverage")) {
				*invert_region_coverage =
					clamp01((float)obs_data_get_double(legacy, "invert_region_coverage"));
			} else if (obs_data_has_user_value(legacy, "white_region_floor")) {
				*invert_region_coverage =
					clamp01((float)obs_data_get_double(legacy, "white_region_floor"));
			}
		}

		obs_data_release(legacy);
	}

	obs_data_release(root);
	return true;
}

bool lenses_policy_load_runtime_file(const char *path, struct lenses_policy_runtime *out_policy,
				     struct lenses_policy_compile_result *out_result)
{
	if (!path || !*path) {
		lenses_policy_set_result(out_result, false, 0, 0, "Policy path is empty");
		return false;
	}

	obs_data_t *root = obs_data_create_from_json_file(path);
	if (!root) {
		lenses_policy_set_result(out_result, false, 0, 0, "Failed to parse policy JSON file");
		return false;
	}

	const bool ok = load_runtime_from_root(root, out_policy, out_result);
	obs_data_release(root);
	return ok;
}

bool lenses_policy_write_legacy_preset(const char *path, const char *preset_id, bool invert_enabled,
				       float invert_strength, float invert_region_threshold,
				       float invert_region_softness,
				       float invert_region_coverage,
				       struct lenses_policy_compile_result *out_result)
{
	if (!path || !*path) {
		lenses_policy_set_result(out_result, false, 0, 0, "Preset output path is empty");
		return false;
	}

	if (!preset_id || !*preset_id)
		preset_id = "legacy-current";

	if (!ensure_parent_directory(path)) {
		lenses_policy_set_result(out_result, false, 0, 0, "Failed to create preset output directory");
		return false;
	}

	obs_data_t *root = obs_data_create();
	obs_data_t *metadata = obs_data_create();
	obs_data_t *model = obs_data_create();
	obs_data_t *runtime = obs_data_create();
	obs_data_t *default_rule = obs_data_create();
	obs_data_t *legacy = obs_data_create();
	obs_data_t *rule = obs_data_create();
	obs_data_t *selector = obs_data_create();
	obs_data_array_t *rules = obs_data_array_create();

	obs_data_set_int(root, "version", LENSES_POLICY_VERSION);
	obs_data_set_string(metadata, "id", preset_id);
	obs_data_set_string(metadata, "name", preset_id);
	obs_data_set_obj(root, "metadata", metadata);

	obs_data_set_string(model, "name", "legacy-invert");
	obs_data_set_obj(root, "model", model);

	obs_data_set_int(runtime, "ai_fps_target", 12);
	obs_data_set_string(runtime, "provider", "noop");
	obs_data_set_obj(root, "runtime", runtime);

	obs_data_set_string(rule, "id", "legacy-invert");
	obs_data_set_bool(rule, "enabled", invert_enabled);
	obs_data_set_int(rule, "priority", 100);
	obs_data_set_string(rule, "region_mode", "include");
	obs_data_set_string(rule, "filter_chain", "invert");
	obs_data_set_string(rule, "blend_mode", "replace");
	obs_data_set_double(rule, "opacity", clamp01(invert_strength));

	obs_data_set_double(selector, "min_confidence", 0.0);
	obs_data_set_obj(rule, "selector", selector);

	obs_data_array_push_back(rules, rule);
	obs_data_set_array(root, "rules", rules);

	obs_data_set_string(default_rule, "id", "default-pass-through");
	obs_data_set_bool(default_rule, "enabled", true);
	obs_data_set_string(default_rule, "filter_chain", "passthrough");
	obs_data_set_string(default_rule, "blend_mode", "replace");
	obs_data_set_obj(root, "default_rule", default_rule);

	obs_data_set_bool(legacy, "invert_enabled", invert_enabled);
	obs_data_set_double(legacy, "invert_strength", clamp01(invert_strength));
	obs_data_set_double(legacy, "invert_region_threshold", clamp01(invert_region_threshold));
	obs_data_set_double(legacy, "invert_region_softness", clamp01(invert_region_softness));
	obs_data_set_double(legacy, "invert_region_coverage", clamp01(invert_region_coverage));
	/* Legacy aliases preserved for backward compatibility with older checkpoints. */
	obs_data_set_double(legacy, "white_bias", clamp01(invert_region_threshold));
	obs_data_set_double(legacy, "white_softness", clamp01(invert_region_softness));
	obs_data_set_double(legacy, "white_region_floor", clamp01(invert_region_coverage));
	obs_data_set_obj(root, "legacy", legacy);

	const bool saved = obs_data_save_json_pretty_safe(root, path, "tmp", "bak");
	if (!saved) {
		lenses_policy_set_result(out_result, false, 0, 0, "Failed to write policy preset to disk");
	} else {
		compile_from_root(root, out_result);
	}

	obs_data_release(metadata);
	obs_data_release(model);
	obs_data_release(runtime);
	obs_data_release(default_rule);
	obs_data_release(legacy);
	obs_data_release(rule);
	obs_data_release(selector);
	obs_data_array_release(rules);
	obs_data_release(root);
	return saved;
}
