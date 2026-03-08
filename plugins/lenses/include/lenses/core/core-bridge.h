#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lenses_core_context;

struct lenses_core_runtime_stats {
	uint64_t submitted_frames;
	uint64_t completed_frames;
	uint64_t dropped_frames;
	uint64_t cadence_skipped_frames;
	uint64_t similarity_skipped_frames;
	uint64_t reused_last_mask_frames;
	uint64_t cloud_timeout_frames;
	uint64_t cloud_fallback_frames;
	double last_latency_ms;
	double last_readback_ms;
	double last_preprocess_ms;
	double last_infer_ms;
	double last_decode_ms;
	double last_track_ms;
	double last_queue_latency_ms;
	double last_end_to_end_latency_ms;
	double submit_fps;
	double complete_fps;
	double drop_fps;
	double readback_ms_p50;
	double readback_ms_p95;
	double readback_ms_p99;
	double preprocess_ms_p50;
	double preprocess_ms_p95;
	double preprocess_ms_p99;
	double infer_ms_p50;
	double infer_ms_p95;
	double infer_ms_p99;
	double decode_ms_p50;
	double decode_ms_p95;
	double decode_ms_p99;
	double track_ms_p50;
	double track_ms_p95;
	double track_ms_p99;
	double queue_latency_ms_p50;
	double queue_latency_ms_p95;
	double queue_latency_ms_p99;
	double end_to_end_latency_ms_p50;
	double end_to_end_latency_ms_p95;
	double end_to_end_latency_ms_p99;
	size_t submit_queue_depth;
	size_t output_queue_depth;
};

#define LENSES_CORE_RUNTIME_BACKEND_MAX 32
#define LENSES_CORE_RUNTIME_DETAIL_MAX 384

struct lenses_core_runtime_health {
	bool ready;
	bool fallback_active;
	bool coreml_requested;
	bool coreml_enabled;
	bool coreml_coverage_known;
	bool cpu_ep_fallback_detected;
	bool cpu_ep_fallback_disabled;
	uint32_t coreml_supported_partitions;
	uint32_t coreml_supported_nodes;
	uint32_t coreml_total_nodes;
	float coreml_coverage_ratio;
	char backend[LENSES_CORE_RUNTIME_BACKEND_MAX];
	char detail[LENSES_CORE_RUNTIME_DETAIL_MAX];
};

struct lenses_core_runtime_config {
	uint32_t ai_fps_target;
	uint32_t input_width;
	uint32_t input_height;
	uint32_t inference_every_n_frames;
	bool enable_similarity_skip;
	float similarity_threshold;
	uint32_t cpu_intra_op_threads;
	uint32_t cpu_inter_op_threads;
	bool enable_iobinding;
	size_t submit_queue_limit;
	size_t output_queue_limit;
	bool strict_runtime_checks;
	bool fallback_to_last_mask;
	uint32_t preprocess_mode;
	uint32_t scheduler_mode;
	uint32_t drop_policy;
	bool profiling_enabled;
	double stage_budget_ms;
	const char *provider;
	const char *execution_provider;
	const char *model_path;
	const char *cloud_endpoint;
	uint32_t cloud_timeout_ms;
	bool model_dynamic_shape;
	bool model_static_input;
	bool model_static_output;
	bool model_supports_iobinding_static_outputs;
};

struct lenses_core_mask_frame_info {
	uint64_t frame_id;
	uint32_t source_width;
	uint32_t source_height;
	uint64_t timestamp_ns;
	size_t instance_count;
	size_t class_mask_count;
};

struct lenses_core_mask_instance {
	uint64_t track_id;
	int32_t class_id;
	float confidence;
	float bbox_x;
	float bbox_y;
	float bbox_width;
	float bbox_height;
	uint64_t mask_handle;
	uint64_t timestamp_ns;
};

struct lenses_core_class_mask {
	int32_t class_id;
	uint64_t mask_handle;
};

struct lenses_core_runtime_snapshot {
	struct lenses_core_runtime_stats stats;
	struct lenses_core_runtime_health health;
	struct lenses_core_mask_frame_info latest_mask_frame;
	bool has_latest_mask_frame;
	bool has_health;
};

struct lenses_core_context *lenses_core_create(void);
void lenses_core_destroy(struct lenses_core_context **context);
void lenses_core_set_runtime_config(struct lenses_core_context *context,
				    const struct lenses_core_runtime_config *config);

void lenses_core_submit_frame(struct lenses_core_context *context, uint64_t frame_id,
			      uint32_t source_width, uint32_t source_height,
			      uint64_t timestamp_ns);
void lenses_core_submit_frame_bgra(struct lenses_core_context *context, uint64_t frame_id,
				   uint32_t source_width, uint32_t source_height,
				   uint64_t timestamp_ns, uint32_t image_width,
				   uint32_t image_height, uint32_t image_linesize,
				   const uint8_t *image_bgra, size_t image_size,
				   double readback_ms);

bool lenses_core_try_get_latest_mask_frame_id(struct lenses_core_context *context,
				      uint64_t *frame_id_out);
bool lenses_core_get_latest_mask_frame_info(struct lenses_core_context *context,
					    struct lenses_core_mask_frame_info *out_info);
size_t lenses_core_copy_latest_mask_instances(struct lenses_core_context *context,
					      struct lenses_core_mask_instance *out_instances,
					      size_t max_instances);
size_t lenses_core_copy_latest_class_masks(struct lenses_core_context *context,
					   struct lenses_core_class_mask *out_class_masks,
					   size_t max_class_masks);
bool lenses_core_copy_mask_bitmap(struct lenses_core_context *context, uint64_t mask_handle,
				  uint8_t *out_data, size_t out_capacity,
				  uint32_t *out_width, uint32_t *out_height,
				  size_t *out_required_bytes);

struct lenses_core_runtime_stats lenses_core_get_runtime_stats(const struct lenses_core_context *context);
bool lenses_core_get_runtime_health(const struct lenses_core_context *context,
				    struct lenses_core_runtime_health *out_health);
bool lenses_core_get_runtime_snapshot(const struct lenses_core_context *context,
				      struct lenses_core_runtime_snapshot *out_snapshot);

#ifdef __cplusplus
}
#endif
