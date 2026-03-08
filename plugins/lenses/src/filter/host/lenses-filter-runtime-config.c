#include "filter/host/lenses-filter-internal.h"

#include <util/bmem.h>
#include <util/platform.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool lenses_resolved_model_path_ready(const struct lenses_filter_data *filter)
{
	if (!filter || !filter->ai_resolved_model_path[0])
		return false;
	return os_file_exists(filter->ai_resolved_model_path);
}

static const char *lenses_builtin_model_id_for_tier(char tier)
{
	switch (tier) {
	case 'n':
		return "yolo11n-seg-coco80";
	case 's':
		return "yolo11s-seg-coco80";
	case 'm':
		return "yolo11m-seg-coco80";
	case 'l':
		return "yolo11l-seg-coco80";
	case 'x':
		return "yolo11x-seg-coco80";
	default:
		return "yolo11s-seg-coco80";
	}
}

static bool lenses_try_resolve_builtin_model_fallback(struct lenses_filter_data *filter, char requested_size)
{
	if (!filter)
		return false;

	const char *model_id = lenses_builtin_model_id_for_tier(requested_size);
	struct dstr rel = {0};
	dstr_printf(&rel, "models/%s/model.onnx", model_id);
	char *builtin_path = rel.array ? obs_module_file(rel.array) : NULL;
	dstr_free(&rel);
	if (!builtin_path)
		return false;

	const bool exists = os_file_exists(builtin_path);
	if (!exists) {
		bfree(builtin_path);
		return false;
	}

	snprintf(filter->ai_resolved_model_path, sizeof(filter->ai_resolved_model_path), "%s",
		 builtin_path);
	snprintf(filter->ai_resolved_model_name, sizeof(filter->ai_resolved_model_name), "%s", model_id);
	filter->ai_resolved_model_builtin = true;
	filter->ai_resolved_model_dynamic_shape = true;
	filter->ai_resolved_model_static_input = false;
	filter->ai_resolved_model_static_output = false;
	filter->ai_resolved_model_supports_iobinding_static_outputs = false;
	bfree(builtin_path);
	return true;
}

uint32_t lenses_parse_input_profile_dim(const char *profile)
{
	if (!profile || !*profile || strcmp(profile, MODEL_INPUT_PROFILE_AUTO) == 0)
		return 0;

	if (strcmp(profile, "416") == 0)
		return 416;
	if (strcmp(profile, "512") == 0)
		return 512;
	if (strcmp(profile, "640") == 0)
		return 640;
	if (strcmp(profile, "768") == 0)
		return 768;
	if (strcmp(profile, "960") == 0)
		return 960;

	return 0;
}

static uint32_t lenses_recommended_auto_input_dim(char size_tier, uint32_t target_fps)
{
	uint32_t dim = 640;
	switch (size_tier) {
	case 'n':
		dim = 640;
		break;
	case 's':
		dim = 512;
		break;
	case 'm':
		dim = 640;
		break;
	case 'l':
		dim = 768;
		break;
	case 'x':
		dim = 960;
		break;
	default:
		dim = 640;
		break;
	}

	/*
	 * Performance-first guardrail for dynamic-shape model packages: keep
	 * default auto profile in a proven realtime envelope at >=12 FPS targets.
	 */
	if (target_fps >= 12) {
		if (size_tier == 'm' && dim > 512)
			dim = 512;
		if ((size_tier == 'l' || size_tier == 'x') && dim > 416)
			dim = 416;
	}
	if (target_fps >= 20 && dim > 416)
		dim = 416;

	return dim;
}

static uint32_t lenses_next_lower_input_dim(uint32_t current_dim)
{
	static const uint32_t levels[] = {960U, 768U, 640U, 512U, 416U};
	for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
		if (levels[i] >= current_dim)
			continue;
		return levels[i];
	}
	return current_dim;
}

static void lenses_collect_available_size_tiers(const struct lenses_model_catalog *catalog, bool out_present[5])
{
	if (!out_present)
		return;

	for (size_t i = 0; i < 5; ++i)
		out_present[i] = false;

	if (!catalog)
		return;

	for (size_t i = 0; i < catalog->count; ++i) {
		const char tier = lenses_normalize_size_tier(catalog->entries[i].size_tier);
		switch (tier) {
		case 'n':
			out_present[0] = true;
			break;
		case 's':
			out_present[1] = true;
			break;
		case 'm':
			out_present[2] = true;
			break;
		case 'l':
			out_present[3] = true;
			break;
		case 'x':
			out_present[4] = true;
			break;
		default:
			break;
		}
	}
}

void lenses_format_available_sizes(const struct lenses_model_catalog *catalog, char *out, size_t out_size)
{
	if (!out || out_size == 0)
		return;

	bool present[5] = {false};
	lenses_collect_available_size_tiers(catalog, present);

	struct dstr text = {0};
	const char *order_names[5] = {"n", "s", "m", "l", "x"};
	bool any = false;
	for (size_t i = 0; i < 5; ++i) {
		if (!present[i])
			continue;
		if (any)
			dstr_cat(&text, ", ");
		dstr_cat(&text, order_names[i]);
		any = true;
	}

	snprintf(out, out_size, "%s", any && text.array ? text.array : "none");
	dstr_free(&text);
}

static char *lenses_policy_get_builtin_path(const char *preset_id)
{
	if (!preset_id || !*preset_id || strcmp(preset_id, POLICY_PRESET_NONE) == 0)
		return NULL;

	if (strcmp(preset_id, POLICY_PRESET_DEFAULT_DARK_MODE) == 0)
		return obs_module_file("presets/default-dark-mode.json");

	return NULL;
}

void lenses_refresh_model_catalog(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	const bool loaded = lenses_model_catalog_reload(&filter->model_catalog, filter->model_catalog_status,
							sizeof(filter->model_catalog_status));
	blog(LOG_INFO, "[lenses] model catalog reload loaded=%d count=%zu status='%s'",
	     loaded ? 1 : 0, filter->model_catalog.count, filter->model_catalog_status);
}

void lenses_resolve_model_selection(struct lenses_filter_data *filter)
{
	if (!filter)
		return;

	filter->ai_resolved_model_path[0] = '\0';
	filter->ai_resolved_model_name[0] = '\0';
	filter->ai_resolved_model_builtin = false;
	filter->ai_resolved_model_dynamic_shape = true;
	filter->ai_resolved_model_static_input = false;
	filter->ai_resolved_model_static_output = false;
	filter->ai_resolved_model_supports_iobinding_static_outputs = false;

	const struct lenses_model_catalog_entry *selected = NULL;
	const bool explicit_model =
		(filter->ai_model_id[0] != '\0' && strcmp(filter->ai_model_id, MODEL_ID_AUTO) != 0);
	const char requested_size = lenses_normalize_size_tier(filter->ai_model_size_tier);

	if (explicit_model) {
		selected = lenses_model_catalog_find_by_id(&filter->model_catalog, filter->ai_model_id);
	}

	if (!selected && requested_size) {
		char tier_string[2] = {requested_size, '\0'};
		selected = lenses_model_catalog_pick_by_size(&filter->model_catalog, tier_string, true);
	}

	/* Legacy compatibility: migrate old path-based config to a catalog entry when possible. */
	if (!selected && filter->ai_model_path[0] != '\0') {
		selected = lenses_model_catalog_find_by_path(&filter->model_catalog, filter->ai_model_path);
	}

	if (!selected)
		selected = lenses_model_catalog_pick_default(&filter->model_catalog);

	if (!selected) {
		if (lenses_try_resolve_builtin_model_fallback(filter, requested_size)) {
			blog(LOG_WARNING,
			     "[lenses] model catalog returned no selection; resolved bundled fallback model='%s'",
			     filter->ai_resolved_model_path);
			return;
		}
		blog(LOG_ERROR,
		     "[lenses] model selection failed: model_id='%s' size_tier='%s' catalog_count=%zu status='%s'",
		     filter->ai_model_id, filter->ai_model_size_tier, filter->model_catalog.count,
		     filter->model_catalog_status);
		return;
	}

	snprintf(filter->ai_resolved_model_path, sizeof(filter->ai_resolved_model_path), "%s",
		 selected->model_path);
	snprintf(filter->ai_resolved_model_name, sizeof(filter->ai_resolved_model_name), "%s",
		 selected->name);
	filter->ai_resolved_model_builtin = selected->built_in;
	filter->ai_resolved_model_dynamic_shape = selected->dynamic_shape;
	filter->ai_resolved_model_static_input = selected->static_input;
	filter->ai_resolved_model_static_output = selected->static_output;
	filter->ai_resolved_model_supports_iobinding_static_outputs =
		selected->supports_iobinding_static_outputs;
	blog(LOG_INFO,
	     "[lenses] resolved model id='%s' name='%s' size_tier='%s' built_in=%d dynamic=%d static_input=%d static_output=%d static_iobinding=%d path='%s'",
	     selected->id, selected->name, selected->size_tier, selected->built_in ? 1 : 0,
	     selected->dynamic_shape ? 1 : 0, selected->static_input ? 1 : 0,
	     selected->static_output ? 1 : 0,
	     selected->supports_iobinding_static_outputs ? 1 : 0, selected->model_path);
}

void lenses_select_ai_input_dimensions(const struct lenses_filter_data *filter,
					      uint32_t *out_width, uint32_t *out_height)
{
	if (!out_width || !out_height)
		return;

	uint32_t width = 640;
	uint32_t height = 640;
	bool forced_profile = false;

	if (filter) {
		const uint32_t forced_dim = lenses_parse_input_profile_dim(filter->ai_input_profile);
		if (forced_dim > 0) {
			width = forced_dim;
			height = forced_dim;
			forced_profile = true;
		} else if (strcmp(filter->ai_input_profile, MODEL_INPUT_PROFILE_AUTO) == 0 &&
			   filter->ai_auto_input_dim_override > 0) {
			width = filter->ai_auto_input_dim_override;
			height = filter->ai_auto_input_dim_override;
			forced_profile = true;
		}
	}

	if (filter && !forced_profile && filter->ai_resolved_model_path[0] != '\0') {
		const struct lenses_model_catalog_entry *resolved_entry =
			lenses_model_catalog_find_by_path(&filter->model_catalog, filter->ai_resolved_model_path);
		if (resolved_entry && resolved_entry->input_width > 0 && resolved_entry->input_height > 0) {
			width = resolved_entry->input_width;
			height = resolved_entry->input_height;
			if (strcmp(filter->ai_input_profile, MODEL_INPUT_PROFILE_AUTO) == 0 &&
			    resolved_entry->dynamic_shape) {
				char tier = lenses_normalize_size_tier(resolved_entry->size_tier);
				if (!tier)
					tier = lenses_normalize_size_tier(filter->ai_model_size_tier);
				const uint32_t target_fps = filter->ai_target_fps ? filter->ai_target_fps : 12U;
				const uint32_t tuned_dim =
					lenses_recommended_auto_input_dim(tier, target_fps);
				if (tuned_dim > 0) {
					width = tuned_dim;
					height = tuned_dim;
				}
			}
		} else {
			const char tier = lenses_normalize_size_tier(filter->ai_model_size_tier);
			const uint32_t target_fps = filter->ai_target_fps ? filter->ai_target_fps : 12U;
			const uint32_t tuned_dim = lenses_recommended_auto_input_dim(tier, target_fps);
			if (tuned_dim > 0) {
				width = tuned_dim;
				height = tuned_dim;
			}
		}
	}

	const uint32_t min_dim = 160;
	const uint32_t max_dim = 2048;
	width = width < min_dim ? min_dim : width;
	height = height < min_dim ? min_dim : height;
	width = width > max_dim ? max_dim : width;
	height = height > max_dim ? max_dim : height;

	*out_width = width;
	*out_height = height;
}

void lenses_maybe_downshift_auto_input_profile(struct lenses_filter_data *filter,
						      const struct lenses_core_runtime_stats *stats,
						      uint64_t now_ns)
{
	if (!filter || !stats || !filter->core)
		return;
	if (strcmp(filter->ai_input_profile, MODEL_INPUT_PROFILE_AUTO) != 0)
		return;
	if (filter->ai_target_fps == 0)
		return;
	if (now_ns - filter->ai_autotune_last_reconfig_ns < 6000000000ULL)
		return;
	if (stats->completed_frames < 12)
		return;

	const double infer_ceiling_fps = lenses_ai_infer_ceiling_fps(stats);
	if (infer_ceiling_fps <= 0.0)
		return;

	const double target_fps = (double)filter->ai_target_fps;
	if (infer_ceiling_fps + 0.5 >= target_fps * 0.95)
		return;

	const size_t submit_limit = filter->ai_submit_queue_limit > 0
					    ? (size_t)filter->ai_submit_queue_limit
					    : 1U;
	const bool queue_backpressure =
		stats->submit_queue_depth + 1U >= submit_limit ||
		(stats->submit_fps > 0.0 && stats->complete_fps + 0.75 < stats->submit_fps);
	if (!queue_backpressure)
		return;

	const uint32_t current_dim = filter->ai_input_width ? filter->ai_input_width : 640U;
	const uint32_t next_dim = lenses_next_lower_input_dim(current_dim);
	if (next_dim >= current_dim)
		return;

	filter->ai_auto_input_dim_override = next_dim;
	filter->ai_input_width = next_dim;
	filter->ai_input_height = next_dim;
	filter->last_ai_submit_ns = 0;
	lenses_apply_runtime_config(filter);
	filter->ai_autotune_last_reconfig_ns = now_ns;

	blog(LOG_WARNING,
	     "[lenses] auto input downshift applied: %ux%u -> %ux%u to reduce sustained backlog (target_fps=%.2f infer_ceiling_fps=%.2f submit_fps=%.2f complete_fps=%.2f)",
	     current_dim, current_dim, next_dim, next_dim, target_fps, infer_ceiling_fps,
	     stats->submit_fps, stats->complete_fps);
}

double lenses_ai_infer_ceiling_fps(const struct lenses_core_runtime_stats *stats)
{
	if (!stats)
		return 0.0;

	double infer_ms = stats->infer_ms_p95;
	if (infer_ms <= 0.0)
		infer_ms = stats->last_infer_ms;
	if (infer_ms <= 0.0)
		return 0.0;

	return 1000.0 / infer_ms;
}

void lenses_policy_compile_and_apply(struct lenses_filter_data *filter, obs_data_t *settings)
{
	if (!filter || !settings)
		return;

	const char *custom_path = obs_data_get_string(settings, SETTING_POLICY_CUSTOM_PATH);
	const char *preset_id = obs_data_get_string(settings, SETTING_POLICY_PRESET_ID);

	char *builtin_path = NULL;
	const char *policy_path = NULL;
	if (custom_path && *custom_path) {
		policy_path = custom_path;
	} else {
		builtin_path = lenses_policy_get_builtin_path(preset_id);
		policy_path = builtin_path;
	}

	if (!policy_path || !*policy_path) {
		struct lenses_policy_compile_result no_policy = {
			.valid = false,
			.rule_count = 0,
			.deterministic_hash = 0,
		};
		snprintf(no_policy.message, sizeof(no_policy.message), "%s",
			 obs_module_text("LensesFilter.PolicyStatus.NoPreset"));
		filter->policy_result = no_policy;
		memset(&filter->policy_runtime, 0, sizeof(filter->policy_runtime));
		filter->policy_runtime_valid = false;
		bfree(builtin_path);
		return;
	}

	bool invert_enabled = filter->invert_enabled;
	float invert_strength = filter->invert_strength;
	float invert_region_threshold = filter->invert_region.threshold;
	float invert_region_softness = filter->invert_region.softness;
	float invert_region_coverage = filter->invert_region.coverage;
	struct lenses_policy_compile_result result = {0};

	const bool loaded = lenses_policy_apply_legacy_overrides_from_file(policy_path, &invert_enabled,
									   &invert_strength,
									   &invert_region_threshold,
									   &invert_region_softness,
									   &invert_region_coverage,
									   &result);
	filter->policy_result = result;
	if (!loaded) {
		blog(LOG_WARNING, "[lenses] Policy load failed for '%s': %s", policy_path, result.message);
		memset(&filter->policy_runtime, 0, sizeof(filter->policy_runtime));
		filter->policy_runtime_valid = false;
		bfree(builtin_path);
		return;
	}

	filter->invert_enabled = invert_enabled;
	filter->invert_strength = invert_strength;
	filter->invert_region.threshold = invert_region_threshold;
	filter->invert_region.softness = invert_region_softness;
	filter->invert_region.coverage = invert_region_coverage;
	lenses_invert_region_clamp(&filter->invert_region);
	filter->policy_runtime_valid =
		lenses_policy_load_runtime_file(policy_path, &filter->policy_runtime, &filter->policy_result);
	if (!filter->policy_runtime_valid) {
		memset(&filter->policy_runtime, 0, sizeof(filter->policy_runtime));
		blog(LOG_WARNING, "[lenses] Policy runtime load failed for '%s': %s", policy_path,
		     filter->policy_result.message);
	}

	bfree(builtin_path);
}

void lenses_apply_runtime_config(struct lenses_filter_data *filter)
{
	if (!filter || !filter->core)
		return;

	const bool ai_lane_active = lenses_filter_ai_lane_active(filter);
	if (!ai_lane_active) {
		filter->ai_input_width = 0;
		filter->ai_input_height = 0;
		filter->ai_auto_input_dim_override = 0;
		filter->last_ai_submit_ns = 0;
		filter->ai_stage_ready = false;
		filter->ai_stage_write_index = 0;
		filter->runtime_prev_stats_ns = 0;
		filter->runtime_prev_submitted_frames = 0;
		filter->runtime_prev_completed_frames = 0;
		filter->runtime_prev_dropped_frames = 0;
		filter->latest_mask_frame_id = 0;

		struct lenses_core_runtime_config disabled_config = {
			.ai_fps_target = 0,
			.input_width = 1,
			.input_height = 1,
			.inference_every_n_frames = 1,
			.enable_similarity_skip = false,
			.similarity_threshold = 0.0f,
			.cpu_intra_op_threads = filter->ai_cpu_intra_threads,
			.cpu_inter_op_threads = filter->ai_cpu_inter_threads,
			.enable_iobinding = false,
			.submit_queue_limit = 1,
			.output_queue_limit = 1,
			.strict_runtime_checks = false,
			.fallback_to_last_mask = false,
			.preprocess_mode = 0,
			.scheduler_mode = 0,
			.drop_policy = 0,
			.profiling_enabled = false,
			.stage_budget_ms = 0.0,
			.provider = "noop",
			.execution_provider = "noop",
			.model_path = "",
			.cloud_endpoint = "",
			.cloud_timeout_ms = 120,
			.model_dynamic_shape = true,
			.model_static_input = false,
			.model_static_output = false,
			.model_supports_iobinding_static_outputs = false,
		};

		lenses_core_set_runtime_config(filter->core, &disabled_config);
		memset(&filter->runtime_health, 0, sizeof(filter->runtime_health));
		(void)lenses_core_get_runtime_health(filter->core, &filter->runtime_health);
		const char *reason = !filter->ai_enabled
					     ? "disabled by user setting"
					     : (filter->ai_target_fps == 0
							? "disabled because target fps is 0"
							: "idle (no active policy/debug mask consumer)");
		blog(LOG_INFO, "[lenses] AI runtime parked (provider=noop): %s", reason);
		return;
	}

	if (!lenses_resolved_model_path_ready(filter)) {
		memset(&filter->runtime_health, 0, sizeof(filter->runtime_health));
		snprintf(filter->runtime_health.backend, sizeof(filter->runtime_health.backend), "%s", "ort");
		snprintf(filter->runtime_health.detail, sizeof(filter->runtime_health.detail),
			 "%s",
			 filter->ai_resolved_model_path[0]
				 ? "resolved model path is not accessible"
				 : "model path is empty");
		blog(LOG_ERROR,
		     "[lenses] strict runtime gate blocked startup before ORT init: model='%s' reason='%s'",
		     filter->ai_resolved_model_path[0] ? filter->ai_resolved_model_path : "(none)",
		     filter->runtime_health.detail);
		return;
	}

	lenses_select_ai_input_dimensions(filter, &filter->ai_input_width, &filter->ai_input_height);
	filter->last_ai_submit_ns = 0;
	filter->ai_stage_ready = false;
	filter->ai_stage_write_index = 0;
	filter->runtime_prev_stats_ns = 0;
	filter->runtime_prev_submitted_frames = 0;
	filter->runtime_prev_completed_frames = 0;
	filter->runtime_prev_dropped_frames = 0;

	struct lenses_core_runtime_config config = {
		.ai_fps_target = filter->ai_target_fps,
		.input_width = filter->ai_input_width,
		.input_height = filter->ai_input_height,
		.inference_every_n_frames = filter->ai_inference_every_n_frames,
		.enable_similarity_skip = filter->ai_similarity_skip,
		.similarity_threshold = filter->ai_similarity_threshold / 100.0f,
		.cpu_intra_op_threads = filter->ai_cpu_intra_threads,
		.cpu_inter_op_threads = filter->ai_cpu_inter_threads,
		.enable_iobinding = filter->ai_enable_iobinding,
			.submit_queue_limit = (size_t)(filter->ai_submit_queue_limit > 0
							       ? filter->ai_submit_queue_limit
							       : 1U),
			.output_queue_limit = (size_t)(filter->ai_output_queue_limit > 0
							       ? filter->ai_output_queue_limit
							       : 1U),
			.strict_runtime_checks = false,
			.fallback_to_last_mask = true,
			.preprocess_mode = filter->ai_preprocess_mode,
			.scheduler_mode = filter->ai_scheduler_mode,
			.drop_policy = filter->ai_drop_policy,
			.profiling_enabled = filter->ai_profiling_enabled,
			.stage_budget_ms = filter->ai_stage_budget_ms,
		.provider = filter->ai_backend,
		.execution_provider = filter->ai_backend,
		.model_path = filter->ai_resolved_model_path,
		.cloud_endpoint = "",
		.cloud_timeout_ms = 120,
		.model_dynamic_shape = filter->ai_resolved_model_dynamic_shape,
		.model_static_input = filter->ai_resolved_model_static_input,
		.model_static_output = filter->ai_resolved_model_static_output,
		.model_supports_iobinding_static_outputs =
			filter->ai_resolved_model_supports_iobinding_static_outputs,
	};

	blog(LOG_INFO,
	     "[lenses] runtime input size selected=%ux%u target_fps=%" PRIu32
	     " profile=%s every_n=%" PRIu32
	     " similarity_skip=%d similarity_threshold=%.2f%% iobinding=%d cpu_threads(intra=%" PRIu32 ",inter=%" PRIu32 ") queues(submit=%" PRIu32 ",output=%" PRIu32 ") backend=%s",
	     filter->ai_input_width, filter->ai_input_height, filter->ai_target_fps,
	     filter->ai_input_profile[0] ? filter->ai_input_profile : MODEL_INPUT_PROFILE_AUTO,
	     filter->ai_inference_every_n_frames, filter->ai_similarity_skip ? 1 : 0,
	     filter->ai_similarity_threshold, filter->ai_enable_iobinding ? 1 : 0,
	     filter->ai_cpu_intra_threads, filter->ai_cpu_inter_threads,
	     filter->ai_submit_queue_limit, filter->ai_output_queue_limit, filter->ai_backend);
	blog(LOG_INFO,
	     "[lenses] runtime modes preprocess=%" PRIu32 " scheduler=%" PRIu32
	     " drop_policy=%" PRIu32 " profiling=%d stage_budget_ms=%.2f model(dynamic=%d static_input=%d static_output=%d static_iobinding=%d)",
	     filter->ai_preprocess_mode, filter->ai_scheduler_mode, filter->ai_drop_policy,
	     filter->ai_profiling_enabled ? 1 : 0, filter->ai_stage_budget_ms,
	     config.model_dynamic_shape ? 1 : 0, config.model_static_input ? 1 : 0,
	     config.model_static_output ? 1 : 0,
	     config.model_supports_iobinding_static_outputs ? 1 : 0);
	blog(LOG_INFO, "[lenses] runtime determinism strict_runtime_checks=%d",
	     config.strict_runtime_checks ? 1 : 0);

	lenses_core_set_runtime_config(filter->core, &config);
	memset(&filter->runtime_health, 0, sizeof(filter->runtime_health));
	(void)lenses_core_get_runtime_health(filter->core, &filter->runtime_health);

	if (!filter->runtime_health.ready) {
		blog(LOG_ERROR,
		     "[lenses] strict runtime gate blocked startup for backend='%s' model='%s' reason='%s'",
		     filter->ai_backend,
		     filter->ai_resolved_model_path[0] ? filter->ai_resolved_model_path : "(none)",
		     filter->runtime_health.detail[0] ? filter->runtime_health.detail : "unknown");
	}

	blog(LOG_INFO,
	     "[lenses] runtime backend=%s ready=%d fallback=%d coreml(requested=%d enabled=%d coverage_known=%d ratio=%.3f supported_nodes=%" PRIu32 " total_nodes=%" PRIu32 " partitions=%" PRIu32 " cpu_fallback=%d cpu_fallback_disabled=%d) detail='%s' model='%s'",
	     filter->runtime_health.backend, filter->runtime_health.ready ? 1 : 0,
	     filter->runtime_health.fallback_active ? 1 : 0,
	     filter->runtime_health.coreml_requested ? 1 : 0,
	     filter->runtime_health.coreml_enabled ? 1 : 0,
	     filter->runtime_health.coreml_coverage_known ? 1 : 0,
	     filter->runtime_health.coreml_coverage_ratio,
	     filter->runtime_health.coreml_supported_nodes,
	     filter->runtime_health.coreml_total_nodes,
	     filter->runtime_health.coreml_supported_partitions,
	     filter->runtime_health.cpu_ep_fallback_detected ? 1 : 0,
	     filter->runtime_health.cpu_ep_fallback_disabled ? 1 : 0,
	     filter->runtime_health.detail,
	     filter->ai_resolved_model_path[0] ? filter->ai_resolved_model_path : "(none)");
}

bool lenses_try_recover_runtime_not_ready(struct lenses_filter_data *filter, uint64_t now_ns,
					  bool force_catalog_reload)
{
	if (!filter || !filter->core)
		return false;
	if (filter->runtime_health.ready)
		return true;

	const uint64_t recovery_interval_ns = 2000000000ULL;
	if (!force_catalog_reload && filter->runtime_recovery_last_attempt_ns != 0 &&
	    now_ns - filter->runtime_recovery_last_attempt_ns < recovery_interval_ns)
		return false;

	filter->runtime_recovery_last_attempt_ns = now_ns;
	const bool model_missing = !lenses_resolved_model_path_ready(filter);
	if (force_catalog_reload || model_missing) {
		lenses_refresh_model_catalog(filter);
		lenses_resolve_model_selection(filter);
	}

	if (!lenses_resolved_model_path_ready(filter)) {
		memset(&filter->runtime_health, 0, sizeof(filter->runtime_health));
		snprintf(filter->runtime_health.backend, sizeof(filter->runtime_health.backend), "%s", "ort");
		snprintf(filter->runtime_health.detail, sizeof(filter->runtime_health.detail),
			 "%s",
			 filter->ai_resolved_model_path[0]
				 ? "resolved model path is not accessible"
				 : "model path is empty");
		blog(LOG_ERROR,
		     "[lenses] runtime recovery blocked: unresolved model path after catalog refresh "
		     "(model='%s' detail='%s')",
		     filter->ai_resolved_model_path[0] ? filter->ai_resolved_model_path : "(none)",
		     filter->runtime_health.detail);
		return false;
	}

	blog(LOG_INFO, "[lenses] attempting runtime recovery with resolved model '%s'",
	     filter->ai_resolved_model_path);
	lenses_apply_runtime_config(filter);
	return filter->runtime_health.ready;
}
