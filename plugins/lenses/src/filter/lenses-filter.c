#include "filter/host/lenses-filter-internal.h"

#include <util/bmem.h>
#include <util/platform.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const uint32_t k_lenses_ai_target_fps_values[LENSES_AI_TARGET_FPS_VALUE_COUNT] = {
	4U, 8U, 12U, 24U, 30U, 48U, 60U, 120U,
};

enum lenses_invert_segment_target_mode {
	LENSES_INVERT_SEGMENT_TARGET_ALL = 0,
	LENSES_INVERT_SEGMENT_TARGET_INCLUDE = 1,
	LENSES_INVERT_SEGMENT_TARGET_EXCLUDE = 2,
};

static inline uint32_t lenses_normalize_ai_target_fps(uint32_t value)
{
	const size_t count = sizeof(k_lenses_ai_target_fps_values) /
			     sizeof(k_lenses_ai_target_fps_values[0]);
	if (count == 0)
		return 12U;

	if (value <= k_lenses_ai_target_fps_values[0])
		return k_lenses_ai_target_fps_values[0];

	for (size_t i = 1; i < count; ++i) {
		const uint32_t next = k_lenses_ai_target_fps_values[i];
		if (value == next)
			return next;
		if (value < next) {
			const uint32_t prev = k_lenses_ai_target_fps_values[i - 1];
			return (value - prev <= next - value) ? prev : next;
		}
	}

	return k_lenses_ai_target_fps_values[count - 1];
}

static void lenses_filter_update(void *data, obs_data_t *settings);
static void lenses_filter_destroy(void *data);

static const char *lenses_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("LensesFilter");
}

char lenses_normalize_size_tier(const char *size_tier)
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

const char *lenses_size_label(char size_tier)
{
	switch (size_tier) {
	case 'n':
		return "Nano";
	case 's':
		return "Small";
	case 'm':
		return "Medium";
	case 'l':
		return "Large";
	case 'x':
		return "XLarge";
	default:
		return "Unknown";
	}
}

static bool lenses_append_unique_class_ids(int *out_ids, size_t *io_count, size_t capacity,
					   const int *ids, size_t id_count)
{
	if (!out_ids || !io_count || !ids)
		return false;

	for (size_t i = 0; i < id_count; ++i) {
		const int class_id = ids[i];
		bool exists = false;
		for (size_t j = 0; j < *io_count; ++j) {
			if (out_ids[j] == class_id) {
				exists = true;
				break;
			}
		}
		if (exists)
			continue;
		if (*io_count >= capacity)
			return false;
		out_ids[*io_count] = class_id;
		(*io_count)++;
	}

	return true;
}

static size_t lenses_collect_invert_target_classes(obs_data_t *settings, int *out_ids, size_t capacity)
{
	if (!settings || !out_ids || capacity == 0)
		return 0;

	static const int people_ids[] = {0};
	static const int animal_ids[] = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
	static const int vehicle_ids[] = {1, 2, 3, 5, 6, 7, 8};

	size_t count = 0;
	if (obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_PEOPLE))
		(void)lenses_append_unique_class_ids(out_ids, &count, capacity, people_ids,
						     OBS_COUNTOF(people_ids));
	if (obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_ANIMALS))
		(void)lenses_append_unique_class_ids(out_ids, &count, capacity, animal_ids,
						     OBS_COUNTOF(animal_ids));
	if (obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_VEHICLES))
		(void)lenses_append_unique_class_ids(out_ids, &count, capacity, vehicle_ids,
						     OBS_COUNTOF(vehicle_ids));

	return count;
}

static void lenses_apply_simple_invert_targeting_policy(struct lenses_filter_data *filter,
							obs_data_t *settings)
{
	if (!filter || !settings)
		return;

	const bool target_people = obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_PEOPLE);
	const bool target_animals = obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_ANIMALS);
	const bool target_vehicles = obs_data_get_bool(settings, SETTING_INVERT_SEGMENT_TARGET_VEHICLES);

	if (!filter->invert_enabled || filter->invert_strength <= 0.0001f) {
		blog(LOG_INFO,
		     "[lenses] simple targeting inactive invert_enabled=%d invert_strength=%.3f mode=all "
		     "targets(people=%d animals=%d vehicles=%d)",
		     filter->invert_enabled ? 1 : 0, filter->invert_strength,
		     target_people ? 1 : 0, target_animals ? 1 : 0, target_vehicles ? 1 : 0);
		return;
	}

	int target_mode = (int)obs_data_get_int(settings, SETTING_INVERT_SEGMENT_TARGET_MODE);
	if (target_mode < LENSES_INVERT_SEGMENT_TARGET_ALL ||
	    target_mode > LENSES_INVERT_SEGMENT_TARGET_EXCLUDE) {
		target_mode = LENSES_INVERT_SEGMENT_TARGET_ALL;
		obs_data_set_int(settings, SETTING_INVERT_SEGMENT_TARGET_MODE, target_mode);
	}
	if (target_mode == LENSES_INVERT_SEGMENT_TARGET_ALL) {
		blog(LOG_INFO,
		     "[lenses] simple targeting inactive mode=all targets(people=%d animals=%d vehicles=%d)",
		     target_people ? 1 : 0, target_animals ? 1 : 0, target_vehicles ? 1 : 0);
		return;
	}

	const bool include_mode = target_mode == LENSES_INVERT_SEGMENT_TARGET_INCLUDE;
	const bool exclude_mode = target_mode == LENSES_INVERT_SEGMENT_TARGET_EXCLUDE;
	const bool include_base_region_chain = filter->invert_component_gate_enabled;

	struct lenses_policy_runtime runtime = {0};
	if (include_mode) {
		snprintf(runtime.default_filter_chain, sizeof(runtime.default_filter_chain), "%s",
			 include_base_region_chain ? "invert" : "passthrough");
	} else {
		snprintf(runtime.default_filter_chain, sizeof(runtime.default_filter_chain), "%s", "invert");
	}

	int class_ids[LENSES_POLICY_MAX_SELECTOR_CLASS_IDS] = {0};
	const size_t class_count = lenses_collect_invert_target_classes(
		settings, class_ids, LENSES_POLICY_MAX_SELECTOR_CLASS_IDS);
	char class_selector_text[128] = {0};
	if (class_count > 0) {
		size_t offset = 0;
		offset += (size_t)snprintf(class_selector_text + offset,
					   sizeof(class_selector_text) - offset, "[");
		const size_t limit = class_count < 8 ? class_count : 8;
		for (size_t i = 0; i < limit && offset < sizeof(class_selector_text); ++i) {
			offset += (size_t)snprintf(class_selector_text + offset,
						   sizeof(class_selector_text) - offset,
						   "%s%d", i == 0 ? "" : ",", class_ids[i]);
		}
		if (class_count > limit && offset < sizeof(class_selector_text))
			(void)snprintf(class_selector_text + offset,
				       sizeof(class_selector_text) - offset, ",...");
		offset = strlen(class_selector_text);
		if (offset < sizeof(class_selector_text))
			(void)snprintf(class_selector_text + offset,
				       sizeof(class_selector_text) - offset, "]");
	} else {
		snprintf(class_selector_text, sizeof(class_selector_text), "[]");
	}
	if (class_count > 0) {
		struct lenses_policy_rule_runtime *rule = &runtime.rules[0];
		snprintf(rule->id, sizeof(rule->id), "%s", "invert-targeting");
		rule->priority = 100;
		rule->class_id_count = class_count;
		rule->class_id = class_ids[0];
		memcpy(rule->class_ids, class_ids, sizeof(class_ids[0]) * class_count);
		rule->region_mode = 0;
		snprintf(rule->filter_chain, sizeof(rule->filter_chain), "%s",
			 include_mode ? "invert-full" : "passthrough");
		snprintf(rule->blend_mode, sizeof(rule->blend_mode), "%s", "replace");
		rule->opacity = 1.0f;
		runtime.rule_count = 1;
	}

	filter->policy_runtime = runtime;
	filter->policy_runtime_valid = true;
	filter->policy_result.valid = true;
	filter->policy_result.rule_count = runtime.rule_count;
	filter->policy_result.deterministic_hash = 0;
	if (include_mode) {
		snprintf(filter->policy_result.message, sizeof(filter->policy_result.message), "%s",
			 runtime.rule_count > 0
				 ? (include_base_region_chain
					    ? "Simple targeting active: invert base regions + selected segments"
					    : "Simple targeting active: invert selected segments only")
				 : (include_base_region_chain
					    ? "Simple targeting active: invert base regions (no selected segments)"
					    : "Simple targeting active: no selected segments"));
	} else {
		snprintf(filter->policy_result.message, sizeof(filter->policy_result.message), "%s",
			 runtime.rule_count > 0
				 ? "Simple targeting active: invert excludes selected segments"
				 : "Simple targeting active: no selected segments to exclude");
	}
	blog(LOG_INFO,
	     "[lenses] simple targeting applied mode=%s class_selector=%s rule_count=%zu default_chain=%s include_base_region=%d",
	     include_mode ? "include" : (exclude_mode ? "exclude" : "all"),
	     class_selector_text, runtime.rule_count, runtime.default_filter_chain,
	     include_base_region_chain ? 1 : 0);
}

static bool lenses_chain_uses_sync_component_mask(const char *chain_name)
{
	return chain_name && strcmp(chain_name, "invert") == 0;
}

static void lenses_deactivate_sync_component_mask(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	filter->invert_component_mask.ready = false;
	filter->invert_component_mask.hue_preview_ready = false;
	filter->invert_component_mask.hue_preview_selected_pixels = 0;
	filter->invert_component_mask.hue_preview_selected_coverage = 0.0f;
	filter->sync_mask_stage_write_index = 0;
	filter->sync_mask_stage_pending_frames = 0;
}

static bool lenses_sync_component_mask_needed_this_frame(const struct lenses_filter_data *filter)
{
	if (!filter)
		return false;

	const bool component_gate_active =
		filter->invert_enabled && filter->invert_strength > 0.0001f &&
		filter->invert_component_gate_enabled;
	if (!component_gate_active)
		return false;

	const bool hue_preview_active =
		filter->debug_enabled && filter->debug_mask_overlay &&
		filter->debug_overlay_mode == LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER;
	if (hue_preview_active)
		return true;

	/*
	 * No policy graph means legacy fallback path, which only runs the
	 * component-gated invert pass.
	 */
	if (!filter->policy_runtime_valid)
		return true;

	if (lenses_chain_uses_sync_component_mask(filter->policy_runtime.default_filter_chain))
		return true;

	for (size_t i = 0; i < filter->policy_runtime.rule_count; ++i) {
		const struct lenses_policy_rule_runtime *rule = &filter->policy_runtime.rules[i];
		if (rule->opacity <= 0.0001f)
			continue;
		if (lenses_chain_uses_sync_component_mask(rule->filter_chain))
			return true;
	}

	return false;
}


static void lenses_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct lenses_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	if (!target || !parent) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);
	if (!width || !height) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (!filter->invert_smoothing_ready) {
		filter->smoothed_invert_strength = filter->invert_strength;
		filter->invert_smoothing_ready = true;
	} else {
		const float blend = fmaxf(0.01f, 1.0f - filter->temporal_smoothing);
		filter->smoothed_invert_strength +=
			(filter->invert_strength - filter->smoothed_invert_strength) * blend;
	}

	filter->frame_counter++;

	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};
	const enum gs_color_space source_space =
		obs_source_get_color_space(target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	const enum gs_color_format format = gs_get_format_from_space(source_space);

	if (!lenses_ensure_render_targets(filter, format)) {
		blog(LOG_WARNING, "[lenses] Failed to allocate render targets");
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (!lenses_capture_target(filter, target, parent, width, height, source_space)) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	gs_texture_t *current_texture = gs_texrender_get_texture(filter->capture_texrender);
	if (!current_texture) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (lenses_sync_component_mask_needed_this_frame(filter))
		lenses_update_sync_component_mask(filter, current_texture, width, height, source_space);
	else
		lenses_deactivate_sync_component_mask(filter);
	const bool ai_lane_active = lenses_filter_ai_lane_active(filter);
	if (ai_lane_active)
		lenses_submit_ai_frame(filter, current_texture, width, height, source_space);
	if (filter->core)
		lenses_update_runtime_diagnostics(filter, os_gettime_ns());

	gs_texture_t *graph_texture = NULL;
	filter->rule_mask_valid = false;
	if (lenses_render_policy_graph(filter, current_texture, width, height, source_space, &graph_texture) &&
	    graph_texture) {
		current_texture = graph_texture;
	} else {
		size_t active_passes = 0;
		for (size_t i = 0; i < filter->pass_count; i++) {
			if (i != LENSES_PASS_INVERT)
				continue;
			struct lenses_pass *pass = &filter->passes[i];
			if (pass->is_enabled && !pass->is_enabled(filter))
				continue;

			gs_texrender_t *output =
				(active_passes % 2 == 0) ? filter->ping_texrender : filter->pong_texrender;
			if (!lenses_pass_render(filter, pass, current_texture, output, width, height,
						source_space))
				continue;

			gs_texture_t *output_texture = gs_texrender_get_texture(output);
			if (!output_texture)
				continue;

			current_texture = output_texture;
			active_passes++;
		}

		size_t legacy_blend_passes = active_passes;
		lenses_apply_debug_overlay(filter, &current_texture, &legacy_blend_passes, width, height,
					   source_space, true);
	}

	lenses_draw_final_texture(current_texture, width, height, source_space);
}

static void lenses_filter_destroy(void *data)
{
	struct lenses_filter_data *filter = data;

	lenses_destroy_passes(filter);
	lenses_destroy_render_targets(filter);
	lenses_core_destroy(&filter->core);
	bfree(filter);
}

static void *lenses_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct lenses_filter_data *filter = bzalloc(sizeof(*filter));
	filter->context = source;
	filter->core = lenses_core_create();

	if (!filter->core)
		blog(LOG_ERROR,
		     "[lenses] Core context init failed; AI runtime unavailable (strict mode, no fallback)");

	if (!lenses_initialize_passes(filter)) {
		lenses_filter_destroy(filter);
		return NULL;
	}

	lenses_filter_update(filter, settings);
	return filter;
}

static void lenses_filter_update(void *data, obs_data_t *settings)
{
	struct lenses_filter_data *filter = data;

	filter->invert_enabled = obs_data_get_bool(settings, SETTING_INVERT_ENABLED);
	filter->invert_strength = (float)obs_data_get_double(settings, SETTING_INVERT_STRENGTH);
	filter->temporal_smoothing = (float)obs_data_get_double(settings, SETTING_TEMPORAL_SMOOTHING);
	const bool has_mask_grow = obs_data_has_user_value(settings, SETTING_MASK_GROW_PX);
	const bool has_mask_expand_legacy =
		obs_data_has_user_value(settings, SETTING_MASK_GROW_PX_LEGACY);
	if (!has_mask_grow && has_mask_expand_legacy)
		obs_data_set_double(settings, SETTING_MASK_GROW_PX,
				    obs_data_get_double(settings, SETTING_MASK_GROW_PX_LEGACY));
	filter->ai_mask_shape.grow_px = (float)obs_data_get_double(settings, SETTING_MASK_GROW_PX);
	filter->ai_mask_shape.shrink_px =
		(float)obs_data_get_double(settings, SETTING_MASK_SHRINK_PX);
	filter->ai_mask_shape.soften_px =
		(float)obs_data_get_double(settings, SETTING_MASK_SOFTEN_PX);
	filter->invert_mask_shape.grow_px =
		(float)obs_data_get_double(settings, SETTING_INVERT_MASK_GROW_PX);
	filter->invert_mask_shape.shrink_px =
		(float)obs_data_get_double(settings, SETTING_INVERT_MASK_SHRINK_PX);
	filter->invert_mask_shape.soften_px =
		(float)obs_data_get_double(settings, SETTING_INVERT_MASK_SOFTEN_PX);
	if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_MIN_AREA_PX))
		obs_data_set_double(settings, SETTING_INVERT_COMPONENT_MIN_AREA_PX,
				    LENSES_INVERT_COMPONENT_MIN_AREA_PX_DEFAULT);
	if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_MIN_SIDE_PX))
		obs_data_set_double(settings, SETTING_INVERT_COMPONENT_MIN_SIDE_PX,
				    LENSES_INVERT_COMPONENT_MIN_SIDE_PX_DEFAULT);
	if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_MIN_FILL))
		obs_data_set_double(settings, SETTING_INVERT_COMPONENT_MIN_FILL,
				    LENSES_INVERT_COMPONENT_MIN_FILL_DEFAULT);
	if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_MIN_COVERAGE))
		obs_data_set_double(settings, SETTING_INVERT_COMPONENT_MIN_COVERAGE,
				    LENSES_INVERT_COMPONENT_MIN_COVERAGE_DEFAULT);
	filter->invert_component_gate_enabled =
		obs_data_get_bool(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED);
	filter->invert_component_min_area_px =
		(float)obs_data_get_double(settings, SETTING_INVERT_COMPONENT_MIN_AREA_PX);
	filter->invert_component_min_side_px =
		(float)obs_data_get_double(settings, SETTING_INVERT_COMPONENT_MIN_SIDE_PX);
	filter->invert_component_min_fill =
		(float)obs_data_get_double(settings, SETTING_INVERT_COMPONENT_MIN_FILL);
	filter->invert_component_min_coverage =
		(float)obs_data_get_double(settings, SETTING_INVERT_COMPONENT_MIN_COVERAGE);
	filter->ai_enabled = obs_data_get_bool(settings, SETTING_AI_ENABLED);
	snprintf(filter->ai_backend, sizeof(filter->ai_backend), "%s",
		 obs_data_get_string(settings, SETTING_AI_BACKEND));
	if (!filter->ai_backend[0])
		snprintf(filter->ai_backend, sizeof(filter->ai_backend), "auto");
	snprintf(filter->ai_model_id, sizeof(filter->ai_model_id), "%s",
		 obs_data_get_string(settings, SETTING_AI_MODEL_ID));
	if (!filter->ai_model_id[0])
		snprintf(filter->ai_model_id, sizeof(filter->ai_model_id), "%s", MODEL_ID_AUTO);
	snprintf(filter->ai_model_path, sizeof(filter->ai_model_path), "%s",
		 obs_data_get_string(settings, SETTING_AI_MODEL_PATH));
	snprintf(filter->ai_model_size_tier, sizeof(filter->ai_model_size_tier), "%s",
		 obs_data_get_string(settings, SETTING_AI_MODEL_SIZE_TIER));
	if (!filter->ai_model_size_tier[0])
		snprintf(filter->ai_model_size_tier, sizeof(filter->ai_model_size_tier), "%s", MODEL_SIZE_AUTO);
	snprintf(filter->ai_input_profile, sizeof(filter->ai_input_profile), "%s",
		 obs_data_get_string(settings, SETTING_AI_INPUT_PROFILE));
	if (!filter->ai_input_profile[0])
		snprintf(filter->ai_input_profile, sizeof(filter->ai_input_profile), "%s",
			 MODEL_INPUT_PROFILE_AUTO);
	filter->ai_auto_input_dim_override = 0;
	filter->ai_autotune_last_reconfig_ns = 0;
	filter->ai_target_fps =
		lenses_normalize_ai_target_fps((uint32_t)obs_data_get_int(settings, SETTING_AI_TARGET_FPS));
	obs_data_set_int(settings, SETTING_AI_TARGET_FPS, (int64_t)filter->ai_target_fps);
	filter->ai_inference_every_n_frames =
		(uint32_t)obs_data_get_int(settings, SETTING_AI_INFERENCE_EVERY_N);
	if (filter->ai_inference_every_n_frames == 0)
		filter->ai_inference_every_n_frames = 1;
	filter->ai_similarity_skip = obs_data_get_bool(settings, SETTING_AI_SIMILARITY_SKIP);
	filter->ai_similarity_threshold =
		(float)obs_data_get_double(settings, SETTING_AI_SIMILARITY_THRESHOLD);
	filter->ai_enable_iobinding = obs_data_get_bool(settings, SETTING_AI_ENABLE_IOBINDING);
	filter->ai_cpu_intra_threads =
		(uint32_t)obs_data_get_int(settings, SETTING_AI_CPU_INTRA_THREADS);
	filter->ai_cpu_inter_threads =
		(uint32_t)obs_data_get_int(settings, SETTING_AI_CPU_INTER_THREADS);
	filter->ai_submit_queue_limit =
		(uint32_t)obs_data_get_int(settings, SETTING_AI_SUBMIT_QUEUE_LIMIT);
	filter->ai_output_queue_limit =
		(uint32_t)obs_data_get_int(settings, SETTING_AI_OUTPUT_QUEUE_LIMIT);
	filter->ai_preprocess_mode = (uint32_t)obs_data_get_int(settings, SETTING_AI_PREPROCESS_MODE);
	filter->ai_scheduler_mode = (uint32_t)obs_data_get_int(settings, SETTING_AI_SCHEDULER_MODE);
	filter->ai_drop_policy = (uint32_t)obs_data_get_int(settings, SETTING_AI_DROP_POLICY);
	filter->ai_profiling_enabled = obs_data_get_bool(settings, SETTING_AI_PROFILING_ENABLED);
	filter->ai_stage_budget_ms = (float)obs_data_get_double(settings, SETTING_AI_STAGE_BUDGET_MS);
	int64_t ai_config_version = obs_data_get_int(settings, SETTING_AI_CONFIG_VERSION);
	if (ai_config_version < 2) {
		/*
		 * Legacy presets from earlier checkpoints commonly persisted tiny queue
		 * defaults (submit=2/output=1), which amplify saturation under heavy
		 * models. Migrate to current baseline guardrails.
		 */
		if (filter->ai_submit_queue_limit <= 2)
			filter->ai_submit_queue_limit = LENSES_AI_SUBMIT_QUEUE_LIMIT;
		if (filter->ai_output_queue_limit <= 1)
			filter->ai_output_queue_limit = LENSES_AI_OUTPUT_QUEUE_LIMIT;
		ai_config_version = 2;
	}
	if (ai_config_version < 3) {
		/*
		 * Introduced collapsible debug group. Force initial state to collapsed
		 * for existing filters and then keep user choice afterward.
		 */
		obs_data_set_bool(settings, SETTING_DEBUG_SECTION_EXPANDED, false);
		ai_config_version = 3;
	}
	if (ai_config_version < 4) {
		/*
		 * Move new installs to simple targeting by default. Keep existing user
		 * selections intact when a policy preset was explicitly chosen.
		 */
		if (!obs_data_has_user_value(settings, SETTING_POLICY_PRESET_ID))
			obs_data_set_string(settings, SETTING_POLICY_PRESET_ID, POLICY_PRESET_NONE);
		ai_config_version = 4;
	}
	if (ai_config_version < 5) {
		/*
		 * Migrate legacy integer expand control to float grow control.
		 */
		if (!obs_data_has_user_value(settings, SETTING_MASK_GROW_PX) &&
		    obs_data_has_user_value(settings, SETTING_MASK_GROW_PX_LEGACY)) {
			obs_data_set_double(settings, SETTING_MASK_GROW_PX,
					    obs_data_get_double(settings, SETTING_MASK_GROW_PX_LEGACY));
		}
		ai_config_version = 5;
	}
	lenses_invert_region_migrate_settings(settings);
	if (ai_config_version < 6) {
		/*
		 * Replace text-aware invert heuristics with region-first controls.
		 */
		ai_config_version = 6;
	}
	if (ai_config_version < 7) {
		/*
		 * Component region gate controls are now explicit.
		 */
		if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED))
			obs_data_set_bool(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED, false);
		ai_config_version = 7;
	}
	if (ai_config_version < 8) {
		/*
		 * Promote component gate to first-class invert tuning control.
		 */
		if (!obs_data_has_user_value(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED))
			obs_data_set_bool(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED, true);
		ai_config_version = 8;
	}
	if (ai_config_version < 9) {
		/*
		 * AI runtime can now be toggled globally in the main settings section.
		 */
		if (!obs_data_has_user_value(settings, SETTING_AI_ENABLED))
			obs_data_set_bool(settings, SETTING_AI_ENABLED, true);
		ai_config_version = 9;
	}
	if (ai_config_version < 10) {
		/*
		 * Extended invert gating now supports luma/saturation/hue ranges.
		 */
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_LUMA_MIN))
			obs_data_set_double(settings, SETTING_INVERT_REGION_LUMA_MIN,
					    LENSES_INVERT_REGION_LUMA_MIN_DEFAULT);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_LUMA_MAX))
			obs_data_set_double(settings, SETTING_INVERT_REGION_LUMA_MAX,
					    LENSES_INVERT_REGION_LUMA_MAX_DEFAULT);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_SAT_MIN))
			obs_data_set_double(settings, SETTING_INVERT_REGION_SAT_MIN,
					    LENSES_INVERT_REGION_SAT_MIN_DEFAULT);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_SAT_MAX))
			obs_data_set_double(settings, SETTING_INVERT_REGION_SAT_MAX,
					    LENSES_INVERT_REGION_SAT_MAX_DEFAULT);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_HUE_MIN_DEGREES))
			obs_data_set_double(settings, SETTING_INVERT_REGION_HUE_MIN_DEGREES,
					    LENSES_INVERT_REGION_HUE_MIN_DEGREES_DEFAULT);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_REGION_HUE_MAX_DEGREES))
			obs_data_set_double(settings, SETTING_INVERT_REGION_HUE_MAX_DEGREES,
					    LENSES_INVERT_REGION_HUE_MAX_DEGREES_DEFAULT);
		ai_config_version = 10;
	}
	if (ai_config_version < 11) {
		/*
		 * Migrate legacy single hue range control to multi-range qualifier.
		 */
		lenses_hue_qualifier_migrate_from_legacy(
			settings,
			(float)obs_data_get_double(settings, SETTING_INVERT_REGION_HUE_MIN_DEGREES),
			(float)obs_data_get_double(settings, SETTING_INVERT_REGION_HUE_MAX_DEGREES));
		ai_config_version = 11;
	}
	if (ai_config_version < 12) {
		/*
		 * Invert component mask shaping now has a dedicated slider bank.
		 */
		if (!obs_data_has_user_value(settings, SETTING_INVERT_MASK_GROW_PX))
			obs_data_set_double(settings, SETTING_INVERT_MASK_GROW_PX, 0.0);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_MASK_SHRINK_PX))
			obs_data_set_double(settings, SETTING_INVERT_MASK_SHRINK_PX, 0.0);
		if (!obs_data_has_user_value(settings, SETTING_INVERT_MASK_SOFTEN_PX))
			obs_data_set_double(settings, SETTING_INVERT_MASK_SOFTEN_PX, 0.0);
		ai_config_version = 12;
	}
	lenses_invert_region_load_settings(settings, &filter->invert_region);
	lenses_hue_qualifier_load_settings(settings, &filter->invert_hue_qualifier);
	obs_data_set_int(settings, SETTING_AI_CONFIG_VERSION, ai_config_version);
	filter->ai_enabled = obs_data_get_bool(settings, SETTING_AI_ENABLED);
	if (filter->ai_cpu_inter_threads == 0)
		filter->ai_cpu_inter_threads = 1;
	filter->debug_enabled = obs_data_get_bool(settings, SETTING_DEBUG_ENABLED);
	const bool has_debug_mask_overlay =
		obs_data_has_user_value(settings, SETTING_DEBUG_MASK_OVERLAY);
	if (!has_debug_mask_overlay)
		obs_data_set_bool(settings, SETTING_DEBUG_MASK_OVERLAY, true);
	filter->debug_mask_overlay =
		filter->debug_enabled && obs_data_get_bool(settings, SETTING_DEBUG_MASK_OVERLAY);
	const bool has_debug_overlay_mode =
		obs_data_has_user_value(settings, SETTING_DEBUG_OVERLAY_MODE);
	if (!has_debug_overlay_mode)
		obs_data_set_int(settings, SETTING_DEBUG_OVERLAY_MODE,
				 LENSES_DEBUG_OVERLAY_MODE_SEGMENTS);
	filter->debug_overlay_mode =
		(uint32_t)obs_data_get_int(settings, SETTING_DEBUG_OVERLAY_MODE);
	if (filter->debug_overlay_mode > LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER) {
		filter->debug_overlay_mode = LENSES_DEBUG_OVERLAY_MODE_SEGMENTS;
		obs_data_set_int(settings, SETTING_DEBUG_OVERLAY_MODE, filter->debug_overlay_mode);
	}
	const bool has_debug_overlay_opacity =
		obs_data_has_user_value(settings, SETTING_DEBUG_OVERLAY_OPACITY);
	if (!has_debug_overlay_opacity)
		obs_data_set_double(settings, SETTING_DEBUG_OVERLAY_OPACITY, 0.35);
	filter->debug_overlay_opacity = (float)obs_data_get_double(settings, SETTING_DEBUG_OVERLAY_OPACITY);
	if (filter->debug_enabled) {
		blog(LOG_INFO,
		     "[lenses] debug overlay config enabled=1 mask_overlay=%d mode=%" PRIu32
		     " opacity=%.2f (mask_overlay_user=%d mode_user=%d opacity_user=%d)",
		     filter->debug_mask_overlay ? 1 : 0, filter->debug_overlay_mode,
		     filter->debug_overlay_opacity, has_debug_mask_overlay ? 1 : 0,
		     has_debug_overlay_mode ? 1 : 0, has_debug_overlay_opacity ? 1 : 0);
	}

	lenses_refresh_model_catalog(filter);
	lenses_resolve_model_selection(filter);
	if (filter->ai_resolved_model_path[0] == '\0') {
		blog(LOG_WARNING,
		     "[lenses] model resolution failed: catalog_count=%zu selected_id='%s' size_tier='%s' status='%s'",
		     filter->model_catalog.count, filter->ai_model_id, filter->ai_model_size_tier,
		     filter->model_catalog_status);
	}

	filter->invert_strength = fmaxf(0.0f, fminf(1.0f, filter->invert_strength));
	lenses_invert_region_clamp(&filter->invert_region);
	lenses_hue_qualifier_clamp(&filter->invert_hue_qualifier);
	filter->temporal_smoothing = fmaxf(0.0f, fminf(0.99f, filter->temporal_smoothing));
	filter->ai_mask_shape.grow_px =
		fmaxf(0.0f, fminf(LENSES_MASK_GROW_MAX_PX, filter->ai_mask_shape.grow_px));
	filter->ai_mask_shape.shrink_px =
		fmaxf(0.0f, fminf(LENSES_MASK_SHRINK_MAX_PX, filter->ai_mask_shape.shrink_px));
	filter->ai_mask_shape.soften_px =
		fmaxf(0.0f, fminf(LENSES_MASK_SOFTEN_MAX_PX, filter->ai_mask_shape.soften_px));
	filter->invert_mask_shape.grow_px =
		fmaxf(0.0f, fminf(LENSES_MASK_GROW_MAX_PX, filter->invert_mask_shape.grow_px));
	filter->invert_mask_shape.shrink_px =
		fmaxf(0.0f, fminf(LENSES_MASK_SHRINK_MAX_PX, filter->invert_mask_shape.shrink_px));
	filter->invert_mask_shape.soften_px =
		fmaxf(0.0f, fminf(LENSES_MASK_SOFTEN_MAX_PX, filter->invert_mask_shape.soften_px));
	filter->invert_component_min_area_px =
		fmaxf(LENSES_INVERT_COMPONENT_MIN_AREA_PX_MIN,
		      fminf(LENSES_INVERT_COMPONENT_MIN_AREA_PX_MAX,
			    filter->invert_component_min_area_px));
	filter->invert_component_min_side_px =
		fmaxf(LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MIN,
		      fminf(LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MAX,
			    filter->invert_component_min_side_px));
	filter->invert_component_min_fill =
		fmaxf(LENSES_INVERT_COMPONENT_MIN_FILL_MIN,
		      fminf(LENSES_INVERT_COMPONENT_MIN_FILL_MAX,
			    filter->invert_component_min_fill));
	filter->invert_component_min_coverage =
		fmaxf(LENSES_INVERT_COMPONENT_MIN_COVERAGE_MIN,
		      fminf(LENSES_INVERT_COMPONENT_MIN_COVERAGE_MAX,
			    filter->invert_component_min_coverage));
	filter->ai_inference_every_n_frames =
		filter->ai_inference_every_n_frames < 1 ? 1 : filter->ai_inference_every_n_frames;
	filter->ai_inference_every_n_frames =
		filter->ai_inference_every_n_frames > 12 ? 12 : filter->ai_inference_every_n_frames;
	filter->ai_similarity_threshold = fmaxf(0.0f, fminf(25.0f, filter->ai_similarity_threshold));
	filter->ai_cpu_intra_threads =
		filter->ai_cpu_intra_threads > 32 ? 32 : filter->ai_cpu_intra_threads;
	filter->ai_cpu_inter_threads =
		filter->ai_cpu_inter_threads < 1 ? 1 : filter->ai_cpu_inter_threads;
	filter->ai_cpu_inter_threads =
		filter->ai_cpu_inter_threads > 8 ? 8 : filter->ai_cpu_inter_threads;
	filter->ai_submit_queue_limit =
		filter->ai_submit_queue_limit < 1 ? 1 : filter->ai_submit_queue_limit;
	filter->ai_submit_queue_limit =
		filter->ai_submit_queue_limit > 16 ? 16 : filter->ai_submit_queue_limit;
	filter->ai_output_queue_limit =
		filter->ai_output_queue_limit < 1 ? 1 : filter->ai_output_queue_limit;
	filter->ai_output_queue_limit =
		filter->ai_output_queue_limit > 8 ? 8 : filter->ai_output_queue_limit;
	filter->ai_preprocess_mode =
		filter->ai_preprocess_mode > 2 ? 2 : filter->ai_preprocess_mode;
	filter->ai_scheduler_mode = filter->ai_scheduler_mode > 2 ? 2 : filter->ai_scheduler_mode;
	filter->ai_drop_policy = filter->ai_drop_policy > 2 ? 2 : filter->ai_drop_policy;
	filter->ai_stage_budget_ms = fmaxf(0.0f, fminf(1000.0f, filter->ai_stage_budget_ms));
	if (lenses_parse_input_profile_dim(filter->ai_input_profile) == 0 &&
	    strcmp(filter->ai_input_profile, MODEL_INPUT_PROFILE_AUTO) != 0) {
		snprintf(filter->ai_input_profile, sizeof(filter->ai_input_profile), "%s",
			 MODEL_INPUT_PROFILE_AUTO);
	}
	filter->debug_overlay_opacity = fmaxf(0.0f, fminf(1.0f, filter->debug_overlay_opacity));
	lenses_policy_compile_and_apply(filter, settings);
	lenses_apply_simple_invert_targeting_policy(filter, settings);
	lenses_apply_runtime_config(filter);
	if (filter->core && !filter->runtime_health.ready)
		(void)lenses_try_recover_runtime_not_ready(filter, os_gettime_ns(), true);
	if (!filter->invert_smoothing_ready) {
		filter->smoothed_invert_strength = filter->invert_strength;
		filter->invert_smoothing_ready = true;
	}
}

static void lenses_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, SETTING_INVERT_ENABLED, true);
	obs_data_set_default_double(settings, SETTING_INVERT_STRENGTH, 1.0);
	lenses_invert_region_set_default_settings(settings);
	lenses_hue_qualifier_set_default_settings(settings);
	obs_data_set_default_double(settings, SETTING_TEMPORAL_SMOOTHING, 0.20);
	obs_data_set_default_double(settings, SETTING_MASK_GROW_PX, 0.0);
	obs_data_set_default_double(settings, SETTING_MASK_SHRINK_PX, 0.0);
	obs_data_set_default_double(settings, SETTING_MASK_SOFTEN_PX, 0.0);
	obs_data_set_default_double(settings, SETTING_INVERT_MASK_GROW_PX, 3.0);
	obs_data_set_default_double(settings, SETTING_INVERT_MASK_SHRINK_PX, 0.0);
	obs_data_set_default_double(settings, SETTING_INVERT_MASK_SOFTEN_PX, 3.0);
	obs_data_set_default_bool(settings, SETTING_INVERT_COMPONENT_GATE_ENABLED, true);
	obs_data_set_default_double(settings, SETTING_INVERT_COMPONENT_MIN_AREA_PX,
				    LENSES_INVERT_COMPONENT_MIN_AREA_PX_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_COMPONENT_MIN_SIDE_PX,
				    LENSES_INVERT_COMPONENT_MIN_SIDE_PX_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_COMPONENT_MIN_FILL,
				    LENSES_INVERT_COMPONENT_MIN_FILL_DEFAULT);
	obs_data_set_default_double(settings, SETTING_INVERT_COMPONENT_MIN_COVERAGE,
				    LENSES_INVERT_COMPONENT_MIN_COVERAGE_DEFAULT);
	obs_data_set_default_string(settings, SETTING_POLICY_PRESET_ID, POLICY_PRESET_NONE);
	obs_data_set_default_string(settings, SETTING_POLICY_CUSTOM_PATH, "");
	obs_data_set_default_bool(settings, SETTING_AI_ENABLED, false);
	obs_data_set_default_string(settings, SETTING_AI_BACKEND, "auto");
	obs_data_set_default_string(settings, SETTING_AI_MODEL_ID, MODEL_ID_AUTO);
	/* Retained only for backward compatibility with legacy config files. */
	obs_data_set_default_string(settings, SETTING_AI_MODEL_PATH, "");
	obs_data_set_default_string(settings, SETTING_AI_MODEL_SIZE_TIER, "n");
	obs_data_set_default_string(settings, SETTING_AI_INPUT_PROFILE, MODEL_INPUT_PROFILE_AUTO);
	obs_data_set_default_int(settings, SETTING_AI_TARGET_FPS, 24);
	obs_data_set_default_int(settings, SETTING_AI_INFERENCE_EVERY_N, 1);
	obs_data_set_default_bool(settings, SETTING_AI_SIMILARITY_SKIP, true);
	obs_data_set_default_double(settings, SETTING_AI_SIMILARITY_THRESHOLD, 2.0);
	obs_data_set_default_bool(settings, SETTING_AI_ENABLE_IOBINDING, true);
	obs_data_set_default_int(settings, SETTING_AI_CPU_INTRA_THREADS, 0);
	obs_data_set_default_int(settings, SETTING_AI_CPU_INTER_THREADS, 1);
	obs_data_set_default_int(settings, SETTING_AI_SUBMIT_QUEUE_LIMIT, LENSES_AI_SUBMIT_QUEUE_LIMIT);
	obs_data_set_default_int(settings, SETTING_AI_OUTPUT_QUEUE_LIMIT, LENSES_AI_OUTPUT_QUEUE_LIMIT);
	obs_data_set_default_int(settings, SETTING_AI_PREPROCESS_MODE, 0);
	obs_data_set_default_int(settings, SETTING_AI_SCHEDULER_MODE, 0);
	obs_data_set_default_int(settings, SETTING_AI_DROP_POLICY, 0);
	obs_data_set_default_bool(settings, SETTING_AI_PROFILING_ENABLED, true);
	obs_data_set_default_double(settings, SETTING_AI_STAGE_BUDGET_MS, 0.0);
	obs_data_set_default_int(settings, SETTING_AI_CONFIG_VERSION, 12);
	obs_data_set_default_bool(settings, SETTING_DEBUG_SECTION_EXPANDED, false);
	obs_data_set_default_bool(settings, SETTING_DEBUG_ENABLED, true);
	obs_data_set_default_bool(settings, SETTING_DEBUG_MASK_OVERLAY, false);
	obs_data_set_default_int(settings, SETTING_DEBUG_OVERLAY_MODE,
				 LENSES_DEBUG_OVERLAY_MODE_SEGMENTS);
	obs_data_set_default_double(settings, SETTING_DEBUG_OVERLAY_OPACITY, 0.35);
	obs_data_set_default_int(settings, SETTING_INVERT_SEGMENT_TARGET_MODE,
				 LENSES_INVERT_SEGMENT_TARGET_EXCLUDE);
	obs_data_set_default_bool(settings, SETTING_INVERT_SEGMENT_TARGET_PEOPLE, true);
	obs_data_set_default_bool(settings, SETTING_INVERT_SEGMENT_TARGET_ANIMALS, false);
	obs_data_set_default_bool(settings, SETTING_INVERT_SEGMENT_TARGET_VEHICLES, false);
}

static enum gs_color_space lenses_filter_get_color_space(void *data, size_t count,
							 const enum gs_color_space *preferred_spaces)
{
	struct lenses_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	if (!target)
		return (count > 0) ? preferred_spaces[0] : GS_CS_SRGB;

	const enum gs_color_space potential_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	enum gs_color_space source_space =
		obs_source_get_color_space(target, OBS_COUNTOF(potential_spaces), potential_spaces);

	if (!count)
		return source_space;

	for (size_t i = 0; i < count; i++) {
		if (preferred_spaces[i] == source_space)
			return source_space;
	}

	return preferred_spaces[0];
}

struct obs_source_info lenses_filter_source = {
	.id = "lenses_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = lenses_filter_get_name,
	.create = lenses_filter_create,
	.destroy = lenses_filter_destroy,
	.update = lenses_filter_update,
	.get_defaults = lenses_filter_defaults,
	.get_properties = lenses_filter_properties,
	.video_render = lenses_filter_render,
	.video_get_color_space = lenses_filter_get_color_space,
};
