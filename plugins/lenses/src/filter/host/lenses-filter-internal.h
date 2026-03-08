#pragma once

#include "lenses/lenses-filter.h"
#include "lenses/core/core-bridge.h"
#include "lenses/io/lenses-model-catalog.h"
#include "lenses/io/lenses-policy.h"
#include "pipeline/lenses-pass.h"
#include "filter/host/lenses-filter-invert-region.h"
#include "filter/host/lenses-filter-hue-qualifier.h"
#include "filter/host/lenses-filter-invert-component-mask.h"
#include "filter/host/lenses-filter-mask-shape.h"

#include <graphics/vec4.h>
#include <obs-source.h>
#include <util/dstr.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTING_INVERT_ENABLED "invert_enabled"
#define SETTING_INVERT_STRENGTH "invert_strength"
#define SETTING_TEMPORAL_SMOOTHING "temporal_smoothing"
#define SETTING_POLICY_PRESET_ID "policy_preset_id"
#define SETTING_POLICY_CUSTOM_PATH "policy_custom_path"
#define SETTING_AI_ENABLED "ai_enabled"
#define SETTING_AI_BACKEND "ai_backend"
#define SETTING_AI_MODEL_ID "ai_model_id"
#define SETTING_AI_MODEL_PATH "ai_model_path"
#define SETTING_AI_MODEL_SIZE_TIER "ai_model_size_tier"
#define SETTING_AI_INPUT_PROFILE "ai_input_profile"
#define SETTING_AI_TARGET_FPS "ai_target_fps"
#define SETTING_AI_INFERENCE_EVERY_N "ai_inference_every_n_frames"
#define SETTING_AI_SIMILARITY_SKIP "ai_similarity_skip"
#define SETTING_AI_SIMILARITY_THRESHOLD "ai_similarity_threshold"
#define SETTING_AI_ENABLE_IOBINDING "ai_enable_iobinding"
#define SETTING_AI_CPU_INTRA_THREADS "ai_cpu_intra_threads"
#define SETTING_AI_CPU_INTER_THREADS "ai_cpu_inter_threads"
#define SETTING_AI_SUBMIT_QUEUE_LIMIT "ai_submit_queue_limit"
#define SETTING_AI_OUTPUT_QUEUE_LIMIT "ai_output_queue_limit"
#define SETTING_AI_PREPROCESS_MODE "ai_preprocess_mode"
#define SETTING_AI_SCHEDULER_MODE "ai_scheduler_mode"
#define SETTING_AI_DROP_POLICY "ai_drop_policy"
#define SETTING_AI_PROFILING_ENABLED "ai_profiling_enabled"
#define SETTING_AI_STAGE_BUDGET_MS "ai_stage_budget_ms"
#define SETTING_AI_CONFIG_VERSION "ai_config_version"
#define SETTING_DEBUG_SECTION_EXPANDED "lenses_debug_section"
#define SETTING_DEBUG_ENABLED "debug_enabled"
#define SETTING_DEBUG_MASK_OVERLAY "debug_mask_overlay"
#define SETTING_DEBUG_OVERLAY_MODE "debug_overlay_mode"
#define SETTING_DEBUG_OVERLAY_OPACITY "debug_overlay_opacity"
#define SETTING_INVERT_SEGMENT_TARGET_MODE "invert_segment_target_mode"
#define SETTING_INVERT_SEGMENT_TARGET_PEOPLE "invert_segment_target_people"
#define SETTING_INVERT_SEGMENT_TARGET_ANIMALS "invert_segment_target_animals"
#define SETTING_INVERT_SEGMENT_TARGET_VEHICLES "invert_segment_target_vehicles"
#define SETTING_INVERT_COMPONENT_GATE_ENABLED "invert_component_gate_enabled"
#define SETTING_INVERT_COMPONENT_MIN_AREA_PX "invert_component_min_area_px"
#define SETTING_INVERT_COMPONENT_MIN_SIDE_PX "invert_component_min_side_px"
#define SETTING_INVERT_COMPONENT_MIN_FILL "invert_component_min_fill"
#define SETTING_INVERT_COMPONENT_MIN_COVERAGE "invert_component_min_coverage"
#define SETTING_MASK_GROW_PX "mask_grow_px"
#define SETTING_MASK_GROW_PX_LEGACY "mask_expand_px"
#define SETTING_MASK_SHRINK_PX "mask_shrink_px"
#define SETTING_MASK_SOFTEN_PX "mask_soften_px"
#define SETTING_INVERT_MASK_GROW_PX "invert_mask_grow_px"
#define SETTING_INVERT_MASK_SHRINK_PX "invert_mask_shrink_px"
#define SETTING_INVERT_MASK_SOFTEN_PX "invert_mask_soften_px"

#define POLICY_PRESET_NONE "none"
#define POLICY_PRESET_DEFAULT_DARK_MODE "default-dark-mode"
#define POLICY_USER_DEFAULT_FILENAME "presets/legacy-current.json"
#define MODEL_ID_AUTO "auto"
#define MODEL_SIZE_AUTO "auto"
#define MODEL_INPUT_PROFILE_AUTO "auto"

#define LENSES_MAX_MASK_INSTANCES 512
#define LENSES_MAX_CLASS_MASKS 128
#define LENSES_AI_SUBMIT_QUEUE_LIMIT 4
#define LENSES_AI_OUTPUT_QUEUE_LIMIT 2
#define LENSES_AI_TARGET_FPS_VALUE_COUNT 8
#define LENSES_SYNC_MASK_STAGE_SURFACE_COUNT 2U

extern const uint32_t k_lenses_ai_target_fps_values[LENSES_AI_TARGET_FPS_VALUE_COUNT];

enum lenses_pass_index {
	LENSES_PASS_INVERT = 0,
	LENSES_PASS_INVERT_FULL = 1,
	LENSES_PASS_GRAYSCALE = 2,
	LENSES_PASS_SOLID_COLOR = 3,
	LENSES_PASS_MASKED_BLEND = 4,
	LENSES_PASS_COUNT = 5,
};

enum lenses_chain_id {
	LENSES_CHAIN_PASSTHROUGH = 0,
	LENSES_CHAIN_INVERT = 1,
	LENSES_CHAIN_INVERT_FULL = 2,
	LENSES_CHAIN_GRAYSCALE = 3,
	LENSES_CHAIN_SOLID_RED = 4,
};

enum lenses_blend_mode_id {
	LENSES_BLEND_REPLACE = 0,
	LENSES_BLEND_ALPHA_MIX = 1,
	LENSES_BLEND_ADD = 2,
	LENSES_BLEND_MULTIPLY = 3,
};

enum lenses_debug_overlay_mode {
	LENSES_DEBUG_OVERLAY_MODE_SEGMENTS = 0,
	LENSES_DEBUG_OVERLAY_MODE_HUE_QUALIFIER = 1,
};

struct invert_pass_params {
	gs_eparam_t *invert_strength;
	gs_eparam_t *region_component_mask;
	gs_eparam_t *use_region_component_mask;
	bool apply_region_component_mask;
};

struct solid_color_pass_params {
	gs_eparam_t *solid_color;
};

struct masked_blend_pass_params {
	gs_eparam_t *overlay_image;
	gs_eparam_t *mask_image;
	gs_eparam_t *opacity;
	gs_eparam_t *blend_mode;
	gs_eparam_t *use_mask;
	gs_eparam_t *region_mode;
};

struct lenses_filter_data {
	obs_source_t *context;
	struct lenses_core_context *core;

	bool invert_enabled;
	float invert_strength;
	float smoothed_invert_strength;
	bool invert_smoothing_ready;
	struct lenses_invert_region_config invert_region;
	struct lenses_invert_hue_range_config invert_hue_qualifier;
	float temporal_smoothing;
	bool invert_component_gate_enabled;
	float invert_component_min_area_px;
	float invert_component_min_side_px;
	float invert_component_min_fill;
	float invert_component_min_coverage;
	struct lenses_mask_shape_params ai_mask_shape;
	struct lenses_mask_shape_params invert_mask_shape;

	gs_texrender_t *capture_texrender;
	gs_texrender_t *ping_texrender;
	gs_texrender_t *pong_texrender;
	gs_texrender_t *graph_work_a;
	gs_texrender_t *graph_work_b;
	gs_texrender_t *chain_invert_texrender;
	gs_texrender_t *chain_invert_full_texrender;
	gs_texrender_t *chain_grayscale_texrender;
	gs_texrender_t *chain_solid_texrender;
	gs_texrender_t *sync_mask_input_texrender;
	gs_texrender_t *sync_mask_luma_texrender;
	gs_stagesurf_t *sync_mask_stage_surfaces[LENSES_SYNC_MASK_STAGE_SURFACE_COUNT];
	size_t sync_mask_stage_write_index;
	size_t sync_mask_stage_pending_frames;
	gs_texrender_t *ai_input_texrender;
	gs_stagesurf_t *ai_stage_surfaces[2];
	size_t ai_stage_write_index;
	bool ai_stage_ready;
	uint64_t last_ai_submit_ns;
	uint64_t ai_diag_last_log_ns;
	uint64_t ai_capacity_last_log_ns;
	float ai_last_frame_luma;
	double ai_last_readback_ms;
	uint32_t ai_input_width;
	uint32_t ai_input_height;
	uint32_t ai_auto_input_dim_override;
	uint64_t ai_autotune_last_reconfig_ns;

	struct lenses_pass passes[8];
	size_t pass_count;

	uint64_t frame_counter;
	uint64_t latest_mask_frame_id;
	struct lenses_policy_compile_result policy_result;
	struct lenses_policy_runtime policy_runtime;
	bool policy_runtime_valid;
	bool ai_enabled;
	char ai_backend[32];
	char ai_model_id[LENSES_MODEL_ID_MAX];
	char ai_model_path[512];
	char ai_model_size_tier[16];
	char ai_input_profile[16];
	char ai_resolved_model_path[LENSES_MODEL_PATH_MAX];
	char ai_resolved_model_name[LENSES_MODEL_NAME_MAX];
	bool ai_resolved_model_builtin;
	bool ai_resolved_model_dynamic_shape;
	bool ai_resolved_model_static_input;
	bool ai_resolved_model_static_output;
	bool ai_resolved_model_supports_iobinding_static_outputs;
	uint32_t ai_target_fps;
	uint32_t ai_inference_every_n_frames;
	bool ai_similarity_skip;
	float ai_similarity_threshold;
	bool ai_enable_iobinding;
	uint32_t ai_cpu_intra_threads;
	uint32_t ai_cpu_inter_threads;
	uint32_t ai_submit_queue_limit;
	uint32_t ai_output_queue_limit;
	uint32_t ai_preprocess_mode;
	uint32_t ai_scheduler_mode;
	uint32_t ai_drop_policy;
	bool ai_profiling_enabled;
	float ai_stage_budget_ms;
	bool debug_enabled;
	bool debug_mask_overlay;
	uint32_t debug_overlay_mode;
	float debug_overlay_opacity;
	struct lenses_model_catalog model_catalog;
	char model_catalog_status[LENSES_MODEL_STATUS_MAX];
	struct lenses_core_runtime_health runtime_health;
	uint64_t runtime_diag_last_log_ns;
	uint64_t runtime_hint_last_log_ns;
	uint64_t runtime_perf_hint_last_log_ns;
	uint64_t runtime_prev_stats_ns;
	uint64_t runtime_prev_submitted_frames;
	uint64_t runtime_prev_completed_frames;
	uint64_t runtime_prev_dropped_frames;
	uint64_t runtime_recovery_last_attempt_ns;

	gs_texture_t *blend_overlay_texture;
	gs_texture_t *blend_mask_texture;
	gs_texture_t *fallback_overlay_texture;
	gs_texture_t *fallback_mask_texture;
	bool blend_use_mask;
	float blend_region_mode;
	float blend_opacity;
	float blend_mode;

	gs_texture_t *rule_mask_texture;
	uint32_t rule_mask_width;
	uint32_t rule_mask_height;
	uint8_t *rule_mask_buffer;
	size_t rule_mask_buffer_capacity;
	struct lenses_mask_shape_context ai_mask_shape_context;
	bool rule_mask_valid;

	gs_texture_t *debug_overlay_texture;
	uint32_t debug_overlay_width;
	uint32_t debug_overlay_height;
	uint8_t *debug_overlay_buffer;
	size_t debug_overlay_buffer_capacity;
	struct lenses_invert_component_mask_context invert_component_mask;

	struct lenses_core_mask_frame_info mask_frame_info;
	struct lenses_core_mask_instance mask_instances[LENSES_MAX_MASK_INSTANCES];
	size_t mask_instance_count;
	struct lenses_core_class_mask class_masks[LENSES_MAX_CLASS_MASKS];
	size_t class_mask_count;
};

char lenses_normalize_size_tier(const char *size_tier);
const char *lenses_size_label(char size_tier);
void lenses_format_available_sizes(const struct lenses_model_catalog *catalog, char *out, size_t out_size);
void lenses_refresh_model_catalog(struct lenses_filter_data *filter);
void lenses_resolve_model_selection(struct lenses_filter_data *filter);
uint32_t lenses_parse_input_profile_dim(const char *profile);
void lenses_select_ai_input_dimensions(const struct lenses_filter_data *filter, uint32_t *out_width,
				       uint32_t *out_height);
void lenses_maybe_downshift_auto_input_profile(struct lenses_filter_data *filter,
						       const struct lenses_core_runtime_stats *stats,
						       uint64_t now_ns);
double lenses_ai_infer_ceiling_fps(const struct lenses_core_runtime_stats *stats);
void lenses_policy_compile_and_apply(struct lenses_filter_data *filter, obs_data_t *settings);
void lenses_apply_runtime_config(struct lenses_filter_data *filter);
bool lenses_try_recover_runtime_not_ready(struct lenses_filter_data *filter, uint64_t now_ns,
					  bool force_catalog_reload);
void lenses_update_runtime_diagnostics(struct lenses_filter_data *filter, uint64_t now_ns);

void lenses_build_runtime_debug_text(struct dstr *out, const struct lenses_filter_data *filter,
				     const struct lenses_core_runtime_stats *stats,
				     const struct lenses_core_runtime_health *runtime_health,
				     const struct lenses_core_mask_frame_info *frame_info,
				     bool has_snapshot);
void lenses_build_detection_debug_text(struct dstr *out, const struct lenses_core_mask_instance *instances,
				       size_t instance_count);
bool lenses_rule_targets_class_masks(const struct lenses_policy_rule_runtime *rule);
bool lenses_filter_policy_requires_ai_masks(const struct lenses_filter_data *filter);
bool lenses_filter_debug_requires_ai_masks(const struct lenses_filter_data *filter);
bool lenses_filter_needs_ai_masks(const struct lenses_filter_data *filter);
bool lenses_filter_ai_lane_active(const struct lenses_filter_data *filter);
void lenses_build_rule_debug_text(struct dstr *out, const struct lenses_filter_data *filter,
				  const struct lenses_core_class_mask *class_masks,
				  size_t class_mask_count);

void lenses_destroy_render_targets(struct lenses_filter_data *filter);
bool lenses_ensure_render_targets(struct lenses_filter_data *filter, enum gs_color_format format);
const char *lenses_get_technique_and_multiplier(enum gs_color_space current_space,
						enum gs_color_space source_space,
						float *multiplier);
void lenses_draw_final_texture(gs_texture_t *texture, uint32_t width, uint32_t height,
			       enum gs_color_space source_space);
bool lenses_capture_target(struct lenses_filter_data *filter, obs_source_t *target, obs_source_t *parent,
			   uint32_t width, uint32_t height, enum gs_color_space source_space);
bool lenses_initialize_passes(struct lenses_filter_data *filter);
void lenses_destroy_passes(struct lenses_filter_data *filter);
void lenses_submit_ai_frame(struct lenses_filter_data *filter, gs_texture_t *source_texture,
			    uint32_t source_width, uint32_t source_height,
			    enum gs_color_space source_space);
void lenses_update_sync_component_mask(struct lenses_filter_data *filter, gs_texture_t *source_texture,
				       uint32_t source_width, uint32_t source_height,
				       enum gs_color_space source_space);
bool lenses_render_policy_graph(struct lenses_filter_data *filter, gs_texture_t *source_texture,
				uint32_t width, uint32_t height,
				enum gs_color_space source_space, gs_texture_t **out_texture);
void lenses_apply_debug_overlay(struct lenses_filter_data *filter, gs_texture_t **io_current_texture,
				size_t *io_blend_passes, uint32_t width, uint32_t height,
				enum gs_color_space source_space, bool refresh_snapshot);

obs_properties_t *lenses_filter_properties(void *data);
