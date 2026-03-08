#include "filter/host/lenses-filter-internal.h"
#include "filter/ui/lenses-hue-qualifier-editor.h"

#include <obs-module.h>
#include <util/bmem.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool lenses_refresh_filter_properties(struct lenses_filter_data *filter)
{
	if (!filter || !filter->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->context);
	obs_source_update(filter->context, settings);
	obs_data_release(settings);
	return true;
}

static bool lenses_policy_reload_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct lenses_filter_data *filter = data;
	return lenses_refresh_filter_properties(filter);
}

static bool lenses_policy_save_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct lenses_filter_data *filter = data;
	if (!filter || !filter->context)
		return false;

	char *output_path = obs_module_config_path(POLICY_USER_DEFAULT_FILENAME);
	struct lenses_policy_compile_result result = {0};
	const bool saved = lenses_policy_write_legacy_preset(output_path, "legacy-current",
							     filter->invert_enabled, filter->invert_strength,
							     filter->invert_region.threshold,
							     filter->invert_region.softness,
							     filter->invert_region.coverage, &result);
	filter->policy_result = result;
	if (!saved)
		blog(LOG_WARNING, "[lenses] Failed to save policy preset '%s': %s", output_path, result.message);

	if (saved) {
		obs_data_t *settings = obs_source_get_settings(filter->context);
		obs_data_set_string(settings, SETTING_POLICY_PRESET_ID, POLICY_PRESET_NONE);
		obs_data_set_string(settings, SETTING_POLICY_CUSTOM_PATH, output_path);
		obs_source_update(filter->context, settings);
		obs_data_release(settings);
	}

	bfree(output_path);
	return true;
}

static bool lenses_models_reload_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct lenses_filter_data *filter = data;
	return lenses_refresh_filter_properties(filter);
}

static bool lenses_debug_refresh_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	return lenses_models_reload_clicked(props, property, data);
}

static bool lenses_hue_editor_open_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct lenses_filter_data *filter = data;
	return lenses_hue_qualifier_open_editor(filter);
}

static bool lenses_class_mask_present(const struct lenses_core_class_mask *class_masks,
				      size_t class_mask_count, int class_id)
{
	if (!class_masks || class_id < 0)
		return false;

	for (size_t i = 0; i < class_mask_count; ++i) {
		if (class_masks[i].class_id == class_id && class_masks[i].mask_handle != 0)
			return true;
	}

	return false;
}

static bool lenses_rule_hits_class_masks(const struct lenses_core_class_mask *class_masks,
					 size_t class_mask_count,
					 const struct lenses_policy_rule_runtime *rule)
{
	if (!rule)
		return false;

	if (rule->class_id_count > 0) {
		const size_t limit = rule->class_id_count < LENSES_POLICY_MAX_SELECTOR_CLASS_IDS
					     ? rule->class_id_count
					     : LENSES_POLICY_MAX_SELECTOR_CLASS_IDS;
		for (size_t i = 0; i < limit; ++i) {
			if (lenses_class_mask_present(class_masks, class_mask_count, rule->class_ids[i]))
				return true;
		}
		return false;
	}

	return lenses_class_mask_present(class_masks, class_mask_count, rule->class_id);
}

static void lenses_append_coreml_runtime_text(struct dstr *out,
					      const struct lenses_core_runtime_health *runtime_health)
{
	if (!out || !runtime_health || !runtime_health->coreml_requested)
		return;

	const char *coreml_status = runtime_health->coreml_enabled ? "enabled" : "not-enabled";
	const char *fallback_status = runtime_health->cpu_ep_fallback_detected ? "yes" : "no";
	const char *fallback_disabled_status =
		runtime_health->cpu_ep_fallback_disabled ? "yes" : "no";
	if (runtime_health->coreml_coverage_known) {
		dstr_catf(out,
			  "\ncoreml=%s coverage=%.1f%% (%" PRIu32 "/%" PRIu32
			  " nodes, partitions=%" PRIu32
			  ") cpu_fallback=%s cpu_fallback_disabled=%s",
			  coreml_status, runtime_health->coreml_coverage_ratio * 100.0f,
			  runtime_health->coreml_supported_nodes, runtime_health->coreml_total_nodes,
			  runtime_health->coreml_supported_partitions, fallback_status,
			  fallback_disabled_status);
	} else {
		dstr_catf(out,
			  "\ncoreml=%s coverage=unknown cpu_fallback=%s cpu_fallback_disabled=%s",
			  coreml_status, runtime_health->coreml_enabled ? "unknown" : "n/a",
			  fallback_disabled_status);
	}
}

void lenses_build_runtime_debug_text(struct dstr *out, const struct lenses_filter_data *filter,
				     const struct lenses_core_runtime_stats *stats,
				     const struct lenses_core_runtime_health *runtime_health,
				     const struct lenses_core_mask_frame_info *frame_info,
				     bool has_snapshot)
{
	if (!out || !filter || !stats)
		return;

	char hue_summary[96] = {0};
	lenses_hue_qualifier_format_band_summary(&filter->invert_hue_qualifier, hue_summary,
						 sizeof(hue_summary));

	if (!has_snapshot || !frame_info) {
		dstr_printf(out,
			    "Runtime\n"
			    "submitted=%" PRIu64 " completed=%" PRIu64 " dropped=%" PRIu64 "\n"
			    "cadence_skip=%" PRIu64 " similarity_skip=%" PRIu64 "\n"
			    "submit_q=%zu output_q=%zu latency=%.2fms submit_fps=%.2f complete_fps=%.2f drop_fps=%.2f\n"
			    "stage_ms(last) readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f\n"
			    "stage_ms(p95) readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f\n"
			    "latest_frame=none latest_mask_frame_id=%" PRIu64,
			    stats->submitted_frames, stats->completed_frames, stats->dropped_frames,
			    stats->cadence_skipped_frames, stats->similarity_skipped_frames,
			    stats->submit_queue_depth, stats->output_queue_depth, stats->last_latency_ms,
			    stats->submit_fps, stats->complete_fps, stats->drop_fps,
			    stats->last_readback_ms, stats->last_preprocess_ms, stats->last_infer_ms,
			    stats->last_decode_ms, stats->last_track_ms, stats->readback_ms_p95,
			    stats->preprocess_ms_p95, stats->infer_ms_p95, stats->decode_ms_p95,
			    stats->track_ms_p95, filter->latest_mask_frame_id);
		dstr_catf(out,
			  "\ncomponent_gate enabled=%d ready=%d coverage=%.3f components=%" PRIu32
			  " min_cov=%.3f min_area=%.0f min_side=%.0f min_fill=%.2f"
			  " luma=[%.2f,%.2f] sat=[%.2f,%.2f] hue_qualifier=%s hue_preview=%.3f",
			  filter->invert_component_gate_enabled ? 1 : 0,
			  filter->invert_component_mask.ready ? 1 : 0,
			  filter->invert_component_mask.accepted_coverage,
			  filter->invert_component_mask.accepted_components,
			  filter->invert_component_min_coverage,
			  filter->invert_component_min_area_px,
			  filter->invert_component_min_side_px,
			  filter->invert_component_min_fill, filter->invert_region.luma_min,
			  filter->invert_region.luma_max, filter->invert_region.saturation_min,
			  filter->invert_region.saturation_max, hue_summary,
			  filter->invert_component_mask.hue_preview_selected_coverage);
		lenses_append_coreml_runtime_text(out, runtime_health);
		return;
	}

	dstr_printf(out,
		    "Runtime\n"
		    "submitted=%" PRIu64 " completed=%" PRIu64 " dropped=%" PRIu64 "\n"
		    "cadence_skip=%" PRIu64 " similarity_skip=%" PRIu64 "\n"
		    "submit_q=%zu output_q=%zu latency=%.2fms submit_fps=%.2f complete_fps=%.2f drop_fps=%.2f\n"
		    "stage_ms(last) readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f\n"
		    "stage_ms(p95) readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f\n"
		    "latest_frame=%" PRIu64 " (%ux%u) instances=%zu class_masks=%zu latest_mask_frame_id=%" PRIu64,
		    stats->submitted_frames, stats->completed_frames, stats->dropped_frames,
		    stats->cadence_skipped_frames, stats->similarity_skipped_frames,
		    stats->submit_queue_depth, stats->output_queue_depth, stats->last_latency_ms,
		    stats->submit_fps, stats->complete_fps, stats->drop_fps,
		    stats->last_readback_ms, stats->last_preprocess_ms, stats->last_infer_ms,
		    stats->last_decode_ms, stats->last_track_ms, stats->readback_ms_p95,
		    stats->preprocess_ms_p95, stats->infer_ms_p95, stats->decode_ms_p95,
		    stats->track_ms_p95, frame_info->frame_id, frame_info->source_width,
		    frame_info->source_height, frame_info->instance_count,
		    frame_info->class_mask_count, filter->latest_mask_frame_id);
	dstr_catf(out,
		  "\ncomponent_gate enabled=%d ready=%d coverage=%.3f components=%" PRIu32
		  " min_cov=%.3f min_area=%.0f min_side=%.0f min_fill=%.2f"
		  " luma=[%.2f,%.2f] sat=[%.2f,%.2f] hue_qualifier=%s hue_preview=%.3f",
		  filter->invert_component_gate_enabled ? 1 : 0,
		  filter->invert_component_mask.ready ? 1 : 0,
		  filter->invert_component_mask.accepted_coverage,
		  filter->invert_component_mask.accepted_components,
		  filter->invert_component_min_coverage,
		  filter->invert_component_min_area_px,
		  filter->invert_component_min_side_px,
		  filter->invert_component_min_fill, filter->invert_region.luma_min,
		  filter->invert_region.luma_max, filter->invert_region.saturation_min,
		  filter->invert_region.saturation_max, hue_summary,
		  filter->invert_component_mask.hue_preview_selected_coverage);
	lenses_append_coreml_runtime_text(out, runtime_health);
}

void lenses_build_detection_debug_text(struct dstr *out, const struct lenses_core_mask_instance *instances,
				       size_t instance_count)
{
	if (!out)
		return;

	if (!instances || instance_count == 0) {
		dstr_printf(out, "Detections\nnone");
		return;
	}

	const size_t max_rows = instance_count < 12 ? instance_count : 12;
	dstr_printf(out, "Detections (%zu shown of %zu)", max_rows, instance_count);
	for (size_t i = 0; i < max_rows; ++i) {
		const struct lenses_core_mask_instance *instance = &instances[i];
		dstr_catf(out,
			  "\n#%zu track=%" PRIu64 " class=%d conf=%.2f bbox=[%.3f, %.3f, %.3f, %.3f]",
			  i + 1, instance->track_id, instance->class_id, instance->confidence,
			  instance->bbox_x, instance->bbox_y, instance->bbox_width,
			  instance->bbox_height);
	}
	if (instance_count > max_rows)
		dstr_catf(out, "\n... +%zu more instances", instance_count - max_rows);
}

void lenses_build_rule_debug_text(struct dstr *out, const struct lenses_filter_data *filter,
				  const struct lenses_core_class_mask *class_masks,
				  size_t class_mask_count)
{
	if (!out || !filter)
		return;

	if (!filter->policy_runtime_valid) {
		dstr_printf(out, "Rule Stack\npolicy runtime unavailable");
		return;
	}

	dstr_printf(out, "Rule Stack (%zu rules, default=%s)", filter->policy_runtime.rule_count,
		    filter->policy_runtime.default_filter_chain);
	dstr_catf(out,
		  "\nMask shaping: ai[grow=%.2fpx shrink=%.2fpx soften=%.2fpx] invert[grow=%.2fpx shrink=%.2fpx soften=%.2fpx]",
		  filter->ai_mask_shape.grow_px, filter->ai_mask_shape.shrink_px,
		  filter->ai_mask_shape.soften_px, filter->invert_mask_shape.grow_px,
		  filter->invert_mask_shape.shrink_px, filter->invert_mask_shape.soften_px);
	for (size_t i = 0; i < filter->policy_runtime.rule_count; ++i) {
		const struct lenses_policy_rule_runtime *rule = &filter->policy_runtime.rules[i];
		const bool has_selector = lenses_rule_targets_class_masks(rule);
		const bool hit = lenses_rule_hits_class_masks(class_masks, class_mask_count, rule);
		const char *status = has_selector ? (hit ? "HIT" : "MISS") : "GLOBAL";
		char selector_text[96] = {0};
		if (rule->class_id_count > 0) {
			size_t offset = 0;
			offset += (size_t)snprintf(selector_text + offset, sizeof(selector_text) - offset,
						   "[");
			const size_t limit = rule->class_id_count < 4 ? rule->class_id_count : 4;
			for (size_t c = 0; c < limit && offset < sizeof(selector_text); ++c) {
				offset += (size_t)snprintf(selector_text + offset,
							   sizeof(selector_text) - offset,
							   "%s%d", c == 0 ? "" : ",",
							   rule->class_ids[c]);
			}
			if (rule->class_id_count > limit && offset < sizeof(selector_text))
				(void)snprintf(selector_text + offset, sizeof(selector_text) - offset, ",...");
			offset = strlen(selector_text);
			if (offset < sizeof(selector_text))
				(void)snprintf(selector_text + offset, sizeof(selector_text) - offset, "]");
		} else if (rule->class_id >= 0) {
			snprintf(selector_text, sizeof(selector_text), "%d", rule->class_id);
		} else {
			snprintf(selector_text, sizeof(selector_text), "*");
		}
		dstr_catf(out,
			  "\n%zu. [%s] id=%s prio=%d class_selector=%s mode=%s chain=%s blend=%s opacity=%.2f",
			  i + 1, status, rule->id, rule->priority, selector_text,
			  rule->region_mode == 1 ? "exclude" : "include", rule->filter_chain,
			  rule->blend_mode, rule->opacity);
	}
}

obs_properties_t *lenses_filter_properties(void *data)
{
	struct lenses_filter_data *filter = data;
	struct lenses_core_runtime_stats runtime_stats = {0};
	struct lenses_core_runtime_snapshot runtime_snapshot = {0};
	struct lenses_core_mask_frame_info debug_frame_info = {0};
	struct lenses_core_mask_instance debug_instances[LENSES_MAX_MASK_INSTANCES] = {0};
	struct lenses_core_class_mask debug_class_masks[LENSES_MAX_CLASS_MASKS] = {0};
	size_t debug_instance_count = 0;
	size_t debug_class_mask_count = 0;
	bool debug_has_snapshot = false;
	struct lenses_core_runtime_health runtime_health = {0};
	bool has_runtime_health = false;

	if (filter && filter->core) {
		if (lenses_core_get_runtime_snapshot(filter->core, &runtime_snapshot)) {
			runtime_stats = runtime_snapshot.stats;
			has_runtime_health = runtime_snapshot.has_health;
			runtime_health = runtime_snapshot.health;
			debug_has_snapshot = runtime_snapshot.has_latest_mask_frame;
			debug_frame_info = runtime_snapshot.latest_mask_frame;
		}
		if (debug_has_snapshot) {
			debug_instance_count = lenses_core_copy_latest_mask_instances(
				filter->core, debug_instances, LENSES_MAX_MASK_INSTANCES);
			debug_class_mask_count =
				lenses_core_copy_latest_class_masks(filter->core, debug_class_masks,
								    LENSES_MAX_CLASS_MASKS);
		}
	}

	obs_properties_t *props = obs_properties_create();
	obs_properties_t *debug_props = obs_properties_create();
	obs_properties_add_text(props, "lenses_dark_mode_info", obs_module_text("LensesFilter.DarkModeInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_text(props, "lenses_ai_info", obs_module_text("LensesFilter.AIInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_bool(props, SETTING_AI_ENABLED, obs_module_text("LensesFilter.AIEnabled"));
	obs_property_t *model_list =
		obs_properties_add_list(props, SETTING_AI_MODEL_ID, obs_module_text("LensesFilter.AIModelPreset"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(model_list, obs_module_text("LensesFilter.AIModelPreset.Auto"),
				     MODEL_ID_AUTO);
	if (filter) {
		for (size_t i = 0; i < filter->model_catalog.count; ++i) {
			const struct lenses_model_catalog_entry *entry = &filter->model_catalog.entries[i];
			char label[320] = {0};
			const char size_tier = lenses_normalize_size_tier(entry->size_tier);
			char size_label[64] = {0};
			char input_label[64] = {0};
			char layout_label[96] = {0};
			if (size_tier)
				snprintf(size_label, sizeof(size_label), ", %s (%c)",
					 lenses_size_label(size_tier), size_tier);
			if (entry->input_width > 0 && entry->input_height > 0)
				snprintf(input_label, sizeof(input_label), ", %ux%u input",
					 entry->input_width, entry->input_height);
			snprintf(layout_label, sizeof(layout_label), ", %s outputs%s",
				 entry->dynamic_shape ? "dynamic" : "static",
				 entry->supports_iobinding_static_outputs ? ", static-io-bind" : "");
			snprintf(label, sizeof(label), "%s (%s%s%s%s, %" PRIu32 " classes)",
				 entry->name, entry->built_in ? "Bundled" : "User", size_label,
				 input_label, layout_label, entry->class_count);
			obs_property_list_add_string(model_list, label, entry->id);
		}
	}
	obs_property_t *model_size_list = obs_properties_add_list(
		props, SETTING_AI_MODEL_SIZE_TIER, obs_module_text("LensesFilter.AIModelQuality"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.Auto"),
				     MODEL_SIZE_AUTO);
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.Nano"), "n");
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.Small"), "s");
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.Medium"), "m");
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.Large"), "l");
	obs_property_list_add_string(model_size_list, obs_module_text("LensesFilter.AIModelQuality.XLarge"), "x");
	obs_property_t *input_profile_list = obs_properties_add_list(
		props, SETTING_AI_INPUT_PROFILE, obs_module_text("LensesFilter.AIInputResolution"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.Auto"),
				     MODEL_INPUT_PROFILE_AUTO);
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.416"), "416");
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.512"), "512");
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.640"), "640");
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.768"), "768");
	obs_property_list_add_string(input_profile_list,
				     obs_module_text("LensesFilter.AIInputResolution.960"), "960");
	obs_properties_add_button(props, "lenses_model_reload",
				  obs_module_text("LensesFilter.AIModelReloadButton"),
				  lenses_models_reload_clicked);
	obs_properties_add_bool(props, SETTING_DEBUG_MASK_OVERLAY,
				obs_module_text("LensesFilter.DebugMaskOverlay"));
	obs_property_t *debug_overlay_mode_list =
		obs_properties_add_list(props, SETTING_DEBUG_OVERLAY_MODE,
					obs_module_text("LensesFilter.DebugOverlayMode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(debug_overlay_mode_list,
				  obs_module_text("LensesFilter.DebugOverlayMode.Segments"),
				  LENSES_DEBUG_OVERLAY_MODE_SEGMENTS);
	obs_property_list_add_int(debug_overlay_mode_list,
				  obs_module_text("LensesFilter.DebugOverlayMode.HueQualifier"),
				  LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER);
	obs_property_t *target_fps_list = obs_properties_add_list(
		props, SETTING_AI_TARGET_FPS, obs_module_text("LensesFilter.AITargetFPS"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (size_t i = 0; i < LENSES_AI_TARGET_FPS_VALUE_COUNT; ++i) {
		char label[16];
		snprintf(label, sizeof(label), "%" PRIu32, k_lenses_ai_target_fps_values[i]);
		obs_property_list_add_int(target_fps_list, label, (int)k_lenses_ai_target_fps_values[i]);
	}
	obs_properties_add_bool(props, SETTING_AI_SIMILARITY_SKIP,
				obs_module_text("LensesFilter.AISimilaritySkip"));
	obs_properties_add_float_slider(props, SETTING_AI_SIMILARITY_THRESHOLD,
					obs_module_text("LensesFilter.AISimilarityThreshold"), 0.0,
					25.0, 0.1);
	if (filter && filter->model_catalog_status[0] != '\0')
		obs_properties_add_text(props, "lenses_model_catalog_status", filter->model_catalog_status,
					OBS_TEXT_INFO);
	if (filter && filter->ai_resolved_model_path[0] != '\0') {
		const struct lenses_model_catalog_entry *resolved_entry =
			lenses_model_catalog_find_by_path(&filter->model_catalog, filter->ai_resolved_model_path);
		char model_runtime_text[768] = {0};
		const char requested_tier = lenses_normalize_size_tier(filter->ai_model_size_tier);
		const bool quality_driven = strcmp(filter->ai_model_id, MODEL_ID_AUTO) == 0;
		const char resolved_tier =
			resolved_entry ? lenses_normalize_size_tier(resolved_entry->size_tier) : '\0';
		char available_sizes[64] = {0};
		lenses_format_available_sizes(&filter->model_catalog, available_sizes, sizeof(available_sizes));
		snprintf(model_runtime_text, sizeof(model_runtime_text),
			 "AI Runtime: %s\nModel: %s\nPath: %s\nRuntime input: %ux%u (profile=%s)\nModel layout: input=%s output=%s static_output_iobinding=%s\nCatalog: %zu package(s)\nAvailable sizes: %s\nRequested quality: %s\nRuntime tuning: target_fps=%" PRIu32 " every_n=%" PRIu32 " similarity_skip=%s threshold=%.1f%% iobinding=%s cpu_threads(intra=%" PRIu32 ",inter=%" PRIu32 ") queues(submit=%" PRIu32 ",output=%" PRIu32 ") preprocess=%" PRIu32 " scheduler=%" PRIu32 " drop_policy=%" PRIu32 " profiling=%s stage_budget=%.1fms%s",
			 filter->ai_enabled ? "enabled" : "disabled",
			 filter->ai_resolved_model_name[0] ? filter->ai_resolved_model_name : "Unknown",
			 filter->ai_resolved_model_path, filter->ai_input_width, filter->ai_input_height,
			 filter->ai_input_profile[0] ? filter->ai_input_profile : MODEL_INPUT_PROFILE_AUTO,
			 resolved_entry && resolved_entry->static_input ? "static" : "dynamic",
			 resolved_entry && resolved_entry->static_output ? "static" : "dynamic",
			 resolved_entry && resolved_entry->supports_iobinding_static_outputs ? "yes"
												: "no",
			 filter->model_catalog.count, available_sizes,
			 requested_tier ? filter->ai_model_size_tier : "auto", filter->ai_target_fps,
			 filter->ai_inference_every_n_frames,
			 filter->ai_similarity_skip ? "on" : "off",
			 filter->ai_similarity_threshold,
			 filter->ai_enable_iobinding ? "on" : "off",
			 filter->ai_cpu_intra_threads, filter->ai_cpu_inter_threads,
			 filter->ai_submit_queue_limit, filter->ai_output_queue_limit,
			 filter->ai_preprocess_mode, filter->ai_scheduler_mode,
			 filter->ai_drop_policy, filter->ai_profiling_enabled ? "on" : "off",
			 filter->ai_stage_budget_ms,
			 quality_driven && resolved_tier && requested_tier && resolved_tier != requested_tier
				 ? "\nStatus: requested quality unavailable, using nearest installed model"
				 : "");
		if (!resolved_entry) {
			snprintf(model_runtime_text + strlen(model_runtime_text),
				 sizeof(model_runtime_text) - strlen(model_runtime_text),
				 "\nStatus: selected model missing from catalog");
		}
		obs_properties_add_text(props, "lenses_model_runtime", model_runtime_text, OBS_TEXT_INFO);
	}
	if (has_runtime_health) {
		char backend_runtime_text[768] = {0};
		if (runtime_health.coreml_requested) {
			const char *coreml_status = runtime_health.coreml_enabled ? "enabled" : "not-enabled";
			if (runtime_health.coreml_coverage_known) {
				snprintf(backend_runtime_text, sizeof(backend_runtime_text),
					 "Backend Status\nbackend=%s ready=%s fallback=%s\ncoreml=%s coverage=%.1f%% (%" PRIu32 "/%" PRIu32 " nodes, partitions=%" PRIu32 ") cpu_fallback=%s cpu_fallback_disabled=%s\n%s",
					 runtime_health.backend[0] ? runtime_health.backend : "unknown",
					 runtime_health.ready ? "yes" : "no",
					 runtime_health.fallback_active ? "yes" : "no", coreml_status,
					 runtime_health.coreml_coverage_ratio * 100.0f,
					 runtime_health.coreml_supported_nodes, runtime_health.coreml_total_nodes,
					 runtime_health.coreml_supported_partitions,
					 runtime_health.cpu_ep_fallback_detected ? "yes" : "no",
					 runtime_health.cpu_ep_fallback_disabled ? "yes" : "no",
					 runtime_health.detail[0] ? runtime_health.detail
								  : "No detail available");
			} else {
				snprintf(backend_runtime_text, sizeof(backend_runtime_text),
					 "Backend Status\nbackend=%s ready=%s fallback=%s\ncoreml=%s coverage=unknown cpu_fallback=%s cpu_fallback_disabled=%s\n%s",
					 runtime_health.backend[0] ? runtime_health.backend : "unknown",
					 runtime_health.ready ? "yes" : "no",
					 runtime_health.fallback_active ? "yes" : "no", coreml_status,
					 runtime_health.coreml_enabled ? "unknown" : "n/a",
					 runtime_health.cpu_ep_fallback_disabled ? "yes" : "no",
					 runtime_health.detail[0] ? runtime_health.detail
								  : "No detail available");
			}
		} else {
			snprintf(backend_runtime_text, sizeof(backend_runtime_text),
				 "Backend Status\nbackend=%s ready=%s fallback=%s\n%s",
				 runtime_health.backend[0] ? runtime_health.backend : "unknown",
				 runtime_health.ready ? "yes" : "no",
				 runtime_health.fallback_active ? "yes" : "no",
				 runtime_health.detail[0] ? runtime_health.detail : "No detail available");
		}
		obs_properties_add_text(debug_props, "lenses_backend_runtime", backend_runtime_text,
					OBS_TEXT_INFO);
	}
	obs_property_t *backend_list =
		obs_properties_add_list(debug_props, SETTING_AI_BACKEND, obs_module_text("LensesFilter.AIBackend"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.Auto"),
				     "auto");
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.Noop"),
				     "noop");
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.ORTCPU"),
				     "ort-cpu");
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.ORTXNNPACK"),
				     "ort-xnnpack");
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.ORTCoreML"),
				     "ort-coreml");
#if defined(LENSES_ENABLE_CLOUD)
	obs_property_list_add_string(backend_list, obs_module_text("LensesFilter.AIBackend.Cloud"),
				     "cloud");
#endif
	obs_properties_add_int(debug_props, SETTING_AI_INFERENCE_EVERY_N,
			       obs_module_text("LensesFilter.AIInferenceEveryNFrames"), 1, 12, 1);
	obs_properties_add_bool(debug_props, SETTING_AI_ENABLE_IOBINDING,
				obs_module_text("LensesFilter.AIEnableIoBinding"));
	obs_properties_add_int_slider(debug_props, SETTING_AI_CPU_INTRA_THREADS,
				      obs_module_text("LensesFilter.AICPUIntraThreads"), 0, 32, 1);
	obs_properties_add_int_slider(debug_props, SETTING_AI_CPU_INTER_THREADS,
				      obs_module_text("LensesFilter.AICPUInterThreads"), 1, 8, 1);
	obs_properties_add_int_slider(debug_props, SETTING_AI_SUBMIT_QUEUE_LIMIT,
				      obs_module_text("LensesFilter.AISubmitQueueLimit"), 1, 16, 1);
	obs_properties_add_int_slider(debug_props, SETTING_AI_OUTPUT_QUEUE_LIMIT,
				      obs_module_text("LensesFilter.AIOutputQueueLimit"), 1, 8, 1);
	obs_property_t *preprocess_mode_list =
		obs_properties_add_list(debug_props, SETTING_AI_PREPROCESS_MODE,
					obs_module_text("LensesFilter.AIPreprocessMode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(preprocess_mode_list,
				  obs_module_text("LensesFilter.AIPreprocessMode.Auto"), 0);
	obs_property_list_add_int(preprocess_mode_list,
				  obs_module_text("LensesFilter.AIPreprocessMode.Scalar"), 1);
	obs_property_list_add_int(preprocess_mode_list,
				  obs_module_text("LensesFilter.AIPreprocessMode.Accelerate"), 2);
	obs_property_t *scheduler_mode_list =
		obs_properties_add_list(debug_props, SETTING_AI_SCHEDULER_MODE,
					obs_module_text("LensesFilter.AISchedulerMode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(scheduler_mode_list,
				  obs_module_text("LensesFilter.AISchedulerMode.Producer"), 0);
	obs_property_list_add_int(scheduler_mode_list,
				  obs_module_text("LensesFilter.AISchedulerMode.Worker"), 1);
	obs_property_list_add_int(scheduler_mode_list,
				  obs_module_text("LensesFilter.AISchedulerMode.Adaptive"), 2);
	obs_property_t *drop_policy_list =
		obs_properties_add_list(debug_props, SETTING_AI_DROP_POLICY,
					obs_module_text("LensesFilter.AIDropPolicy"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(drop_policy_list,
				  obs_module_text("LensesFilter.AIDropPolicy.DropOldest"), 0);
	obs_property_list_add_int(drop_policy_list,
				  obs_module_text("LensesFilter.AIDropPolicy.DropNewest"), 1);
	obs_property_list_add_int(drop_policy_list,
				  obs_module_text("LensesFilter.AIDropPolicy.BlockNever"), 2);
	obs_properties_add_bool(debug_props, SETTING_AI_PROFILING_ENABLED,
				obs_module_text("LensesFilter.AIProfilingEnabled"));
	obs_properties_add_float_slider(debug_props, SETTING_AI_STAGE_BUDGET_MS,
					obs_module_text("LensesFilter.AIStageBudgetMs"), 0.0,
					100.0, 0.5);
	obs_properties_add_text(debug_props, "lenses_policy_info", obs_module_text("LensesFilter.PolicyInfo"),
				OBS_TEXT_INFO);
	obs_property_t *preset_list =
		obs_properties_add_list(debug_props, SETTING_POLICY_PRESET_ID,
					obs_module_text("LensesFilter.PolicyPreset"), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset_list, obs_module_text("LensesFilter.PolicyPreset.None"),
				     POLICY_PRESET_NONE);
	obs_property_list_add_string(preset_list, obs_module_text("LensesFilter.PolicyPreset.DefaultDarkMode"),
				     POLICY_PRESET_DEFAULT_DARK_MODE);
	obs_properties_add_path(debug_props, SETTING_POLICY_CUSTOM_PATH,
				obs_module_text("LensesFilter.PolicyCustomPath"), OBS_PATH_FILE,
				obs_module_text("LensesFilter.PolicyPathFilter"), NULL);
	obs_properties_add_button(debug_props, "lenses_policy_reload",
				  obs_module_text("LensesFilter.PolicyReloadButton"),
				  lenses_policy_reload_clicked);
	obs_properties_add_button(debug_props, "lenses_policy_save",
				  obs_module_text("LensesFilter.PolicySaveButton"),
				  lenses_policy_save_clicked);
	if (filter && filter->policy_result.message[0] != '\0')
		obs_properties_add_text(debug_props, "lenses_policy_status", filter->policy_result.message,
					OBS_TEXT_INFO);
	obs_properties_add_bool(debug_props, SETTING_DEBUG_ENABLED, obs_module_text("LensesFilter.DebugEnabled"));
	if (filter && filter->debug_enabled) {
		struct dstr runtime_text = {0};
		struct dstr detection_text = {0};
		struct dstr rule_stack_text = {0};
		lenses_build_runtime_debug_text(&runtime_text, filter, &runtime_stats,
						has_runtime_health ? &runtime_health : NULL,
						debug_has_snapshot ? &debug_frame_info : NULL,
						debug_has_snapshot);
		lenses_build_detection_debug_text(&detection_text, debug_instances, debug_instance_count);
		lenses_build_rule_debug_text(&rule_stack_text, filter, debug_class_masks,
					     debug_class_mask_count);
		obs_properties_add_text(debug_props, "lenses_runtime_info",
					runtime_text.array ? runtime_text.array : "", OBS_TEXT_INFO);
		obs_properties_add_text(debug_props, "lenses_detection_info",
					detection_text.array ? detection_text.array : "", OBS_TEXT_INFO);
		obs_properties_add_text(debug_props, "lenses_rule_stack_info",
					rule_stack_text.array ? rule_stack_text.array : "",
					OBS_TEXT_INFO);
			dstr_free(&runtime_text);
			dstr_free(&detection_text);
			dstr_free(&rule_stack_text);
			obs_properties_add_button(debug_props, "lenses_debug_refresh",
						  obs_module_text("LensesFilter.DebugRefreshButton"),
						  lenses_debug_refresh_clicked);
			obs_properties_add_float_slider(debug_props, SETTING_DEBUG_OVERLAY_OPACITY,
							obs_module_text("LensesFilter.DebugOverlayOpacity"), 0.0,
							1.0, 0.01);
		}
	obs_properties_add_group(props, SETTING_DEBUG_SECTION_EXPANDED,
				 obs_module_text("LensesFilter.DebugSection"), OBS_GROUP_NORMAL,
				 debug_props);

	obs_properties_add_text(props, "lenses_segment_targeting_info",
				obs_module_text("LensesFilter.SegmentTargetingInfo"), OBS_TEXT_INFO);
	obs_property_t *target_mode_list =
		obs_properties_add_list(props, SETTING_INVERT_SEGMENT_TARGET_MODE,
					obs_module_text("LensesFilter.SegmentTargetingMode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(target_mode_list, obs_module_text("LensesFilter.SegmentTargetingMode.All"), 0);
	obs_property_list_add_int(target_mode_list,
				  obs_module_text("LensesFilter.SegmentTargetingMode.Include"), 1);
	obs_property_list_add_int(target_mode_list,
				  obs_module_text("LensesFilter.SegmentTargetingMode.Exclude"), 2);
	obs_properties_add_bool(props, SETTING_INVERT_SEGMENT_TARGET_PEOPLE,
				obs_module_text("LensesFilter.SegmentTargetPeople"));
	obs_properties_add_bool(props, SETTING_INVERT_SEGMENT_TARGET_ANIMALS,
				obs_module_text("LensesFilter.SegmentTargetAnimals"));
	obs_properties_add_bool(props, SETTING_INVERT_SEGMENT_TARGET_VEHICLES,
				obs_module_text("LensesFilter.SegmentTargetVehicles"));
	obs_properties_add_text(props, "lenses_mask_shaping_info",
				obs_module_text("LensesFilter.AIMaskShapingInfo"), OBS_TEXT_INFO);
	obs_properties_add_float_slider(props, SETTING_MASK_GROW_PX,
					obs_module_text("LensesFilter.AIMaskGrowPixels"), 0.0,
					LENSES_MASK_GROW_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_MASK_SHRINK_PX,
					obs_module_text("LensesFilter.AIMaskShrinkPixels"), 0.0,
					LENSES_MASK_SHRINK_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_MASK_SOFTEN_PX,
					obs_module_text("LensesFilter.AIMaskSoftenPixels"), 0.0,
					LENSES_MASK_SOFTEN_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);

	obs_properties_add_bool(props, SETTING_INVERT_ENABLED, obs_module_text("LensesFilter.InvertEnabled"));
	obs_properties_add_float_slider(props, SETTING_INVERT_STRENGTH,
					obs_module_text("LensesFilter.InvertStrength"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_THRESHOLD,
					obs_module_text("LensesFilter.InvertRegionThreshold"),
					LENSES_INVERT_REGION_THRESHOLD_MIN,
					LENSES_INVERT_REGION_THRESHOLD_MAX,
					LENSES_INVERT_REGION_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_SOFTNESS,
					obs_module_text("LensesFilter.InvertRegionSoftness"),
					LENSES_INVERT_REGION_SOFTNESS_MIN,
					LENSES_INVERT_REGION_SOFTNESS_MAX,
					LENSES_INVERT_REGION_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_COVERAGE,
					obs_module_text("LensesFilter.InvertRegionCoverage"),
					LENSES_INVERT_REGION_COVERAGE_MIN,
					LENSES_INVERT_REGION_COVERAGE_MAX,
					LENSES_INVERT_REGION_SLIDER_STEP);
	obs_properties_add_text(props, "lenses_invert_color_gates_info",
				obs_module_text("LensesFilter.InvertColorGatesInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_LUMA_MIN,
					obs_module_text("LensesFilter.InvertRegionLumaMin"),
					LENSES_INVERT_REGION_LUMA_MIN_MIN,
					LENSES_INVERT_REGION_LUMA_MIN_MAX, 0.01);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_LUMA_MAX,
					obs_module_text("LensesFilter.InvertRegionLumaMax"),
					LENSES_INVERT_REGION_LUMA_MAX_MIN,
					LENSES_INVERT_REGION_LUMA_MAX_MAX, 0.01);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_SAT_MIN,
					obs_module_text("LensesFilter.InvertRegionSaturationMin"),
					LENSES_INVERT_REGION_SAT_MIN_MIN,
					LENSES_INVERT_REGION_SAT_MIN_MAX, 0.01);
	obs_properties_add_float_slider(props, SETTING_INVERT_REGION_SAT_MAX,
					obs_module_text("LensesFilter.InvertRegionSaturationMax"),
					LENSES_INVERT_REGION_SAT_MAX_MIN,
					LENSES_INVERT_REGION_SAT_MAX_MAX, 0.01);
	obs_properties_add_text(props, "lenses_invert_hue_qualifier_info",
				obs_module_text("LensesFilter.InvertHueQualifierInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_bool(props, SETTING_INVERT_HUE_QUALIFIER_ENABLED,
				obs_module_text("LensesFilter.InvertHueQualifierEnabled"));
	obs_property_t *hue_mode_list =
		obs_properties_add_list(props, SETTING_INVERT_HUE_QUALIFIER_MODE,
					obs_module_text("LensesFilter.InvertHueQualifierMode"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(hue_mode_list,
				  obs_module_text("LensesFilter.InvertHueQualifierMode.Exclude"),
				  LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE);
	obs_property_list_add_int(hue_mode_list,
				  obs_module_text("LensesFilter.InvertHueQualifierMode.Include"),
				  LENSES_INVERT_HUE_RANGE_MODE_INCLUDE);
	obs_properties_add_button(props, "lenses_invert_hue_editor_open",
				  obs_module_text("LensesFilter.InvertHueEditorOpen"),
				  lenses_hue_editor_open_clicked);
	obs_properties_add_text(props, "lenses_invert_hue_editor_info",
				obs_module_text("LensesFilter.InvertHueEditorInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_text(props, "lenses_invert_component_gate_info",
				obs_module_text("LensesFilter.InvertComponentGateInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_bool(props, SETTING_INVERT_COMPONENT_GATE_ENABLED,
				obs_module_text("LensesFilter.InvertComponentGateEnabled"));
	obs_properties_add_text(props, "lenses_invert_mask_shaping_info",
				obs_module_text("LensesFilter.InvertMaskShapingInfo"), OBS_TEXT_INFO);
	obs_properties_add_float_slider(props, SETTING_INVERT_MASK_GROW_PX,
					obs_module_text("LensesFilter.InvertMaskGrowPixels"), 0.0,
					LENSES_MASK_GROW_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_INVERT_MASK_SHRINK_PX,
					obs_module_text("LensesFilter.InvertMaskShrinkPixels"), 0.0,
					LENSES_MASK_SHRINK_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_INVERT_MASK_SOFTEN_PX,
					obs_module_text("LensesFilter.InvertMaskSoftenPixels"), 0.0,
					LENSES_MASK_SOFTEN_MAX_PX, LENSES_MASK_SHAPE_SLIDER_STEP);
	obs_properties_add_float_slider(props, SETTING_INVERT_COMPONENT_MIN_AREA_PX,
					obs_module_text("LensesFilter.InvertComponentMinAreaPx"),
					LENSES_INVERT_COMPONENT_MIN_AREA_PX_MIN,
					LENSES_INVERT_COMPONENT_MIN_AREA_PX_MAX, 1.0);
	obs_properties_add_float_slider(props, SETTING_INVERT_COMPONENT_MIN_SIDE_PX,
					obs_module_text("LensesFilter.InvertComponentMinSidePx"),
					LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MIN,
					LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MAX, 1.0);
	obs_properties_add_float_slider(props, SETTING_INVERT_COMPONENT_MIN_FILL,
					obs_module_text("LensesFilter.InvertComponentMinFill"),
					LENSES_INVERT_COMPONENT_MIN_FILL_MIN,
					LENSES_INVERT_COMPONENT_MIN_FILL_MAX, 0.01);
	obs_properties_add_float_slider(props, SETTING_INVERT_COMPONENT_MIN_COVERAGE,
					obs_module_text("LensesFilter.InvertComponentMinCoverage"),
					LENSES_INVERT_COMPONENT_MIN_COVERAGE_MIN,
					LENSES_INVERT_COMPONENT_MIN_COVERAGE_MAX, 0.001);
	obs_properties_add_float_slider(props, SETTING_TEMPORAL_SMOOTHING,
					obs_module_text("LensesFilter.TemporalSmoothing"), 0.0, 0.99, 0.01);
	return props;
}
