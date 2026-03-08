#pragma once

#include "lenses/core/types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lenses::core {

enum class PreprocessMode : uint8_t {
	Auto = 0,
	Scalar = 1,
	Accelerate = 2,
};

enum class SchedulerMode : uint8_t {
	ProducerTimed = 0,
	WorkerTimed = 1,
	Adaptive = 2,
};

enum class DropPolicy : uint8_t {
	DropOldest = 0,
	DropNewest = 1,
	BlockNever = 2,
};

struct RuntimeConfig {
	uint32_t ai_fps_target = 12;
	uint32_t input_width = 960;
	uint32_t input_height = 540;
	uint32_t inference_every_n_frames = 1;
	bool enable_similarity_skip = true;
	float similarity_threshold = 0.02f;
	uint32_t cpu_intra_op_threads = 0;
	uint32_t cpu_inter_op_threads = 1;
	bool enable_iobinding = true;
	std::string provider = "auto";
	std::string model_path;
	std::string execution_provider = "auto";
	std::string cloud_endpoint;
	uint32_t cloud_timeout_ms = 120;
	bool model_dynamic_shape = true;
	bool model_static_input = false;
	bool model_static_output = false;
	bool model_supports_iobinding_static_outputs = false;
	size_t submit_queue_limit = 4;
	size_t output_queue_limit = 2;
	bool strict_runtime_checks = true;
	bool fallback_to_last_mask = true;
	PreprocessMode preprocess_mode = PreprocessMode::Auto;
	SchedulerMode scheduler_mode = SchedulerMode::ProducerTimed;
	DropPolicy drop_policy = DropPolicy::DropOldest;
	bool profiling_enabled = false;
	double stage_budget_ms = 0.0;
};

struct FrameTicket {
	uint64_t frame_id = 0;
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	uint64_t timestamp_ns = 0;
	uint32_t image_width = 0;
	uint32_t image_height = 0;
	uint32_t image_linesize = 0;
	double readback_ms = 0.0;
	uint64_t runtime_submit_ns = 0;
	std::vector<uint8_t> image_bgra;
};

struct MaskGeneratorStats {
	uint64_t submitted_frames = 0;
	uint64_t completed_frames = 0;
	uint64_t dropped_frames = 0;
	uint64_t cadence_skipped_frames = 0;
	uint64_t similarity_skipped_frames = 0;
	uint64_t reused_last_mask_frames = 0;
	uint64_t cloud_timeout_frames = 0;
	uint64_t cloud_fallback_frames = 0;
	double last_latency_ms = 0.0;
	double last_readback_ms = 0.0;
	double last_preprocess_ms = 0.0;
	double last_infer_ms = 0.0;
	double last_decode_ms = 0.0;
	double last_track_ms = 0.0;
	double last_queue_latency_ms = 0.0;
	double last_end_to_end_latency_ms = 0.0;
	double submit_fps = 0.0;
	double complete_fps = 0.0;
	double drop_fps = 0.0;
	double readback_ms_p50 = 0.0;
	double readback_ms_p95 = 0.0;
	double readback_ms_p99 = 0.0;
	double preprocess_ms_p50 = 0.0;
	double preprocess_ms_p95 = 0.0;
	double preprocess_ms_p99 = 0.0;
	double infer_ms_p50 = 0.0;
	double infer_ms_p95 = 0.0;
	double infer_ms_p99 = 0.0;
	double decode_ms_p50 = 0.0;
	double decode_ms_p95 = 0.0;
	double decode_ms_p99 = 0.0;
	double track_ms_p50 = 0.0;
	double track_ms_p95 = 0.0;
	double track_ms_p99 = 0.0;
	double queue_latency_ms_p50 = 0.0;
	double queue_latency_ms_p95 = 0.0;
	double queue_latency_ms_p99 = 0.0;
	double end_to_end_latency_ms_p50 = 0.0;
	double end_to_end_latency_ms_p95 = 0.0;
	double end_to_end_latency_ms_p99 = 0.0;
	size_t submit_queue_depth = 0;
	size_t output_queue_depth = 0;
};

struct MaskGeneratorHealth {
	bool ready = false;
	bool fallback_active = false;
	std::string backend = "unknown";
	std::string detail = "No runtime status available";
	bool coreml_requested = false;
	bool coreml_enabled = false;
	bool coreml_coverage_known = false;
	bool cpu_ep_fallback_detected = false;
	bool cpu_ep_fallback_disabled = false;
	uint32_t coreml_supported_partitions = 0;
	uint32_t coreml_supported_nodes = 0;
	uint32_t coreml_total_nodes = 0;
	float coreml_coverage_ratio = 0.0f;
};

struct ComposeRequest {
	uint64_t frame_id = 0;
	uint32_t source_width = 0;
	uint32_t source_height = 0;
};

struct ComposeResult {
	std::vector<std::string> applied_rule_ids;
};

class IMaskGenerator {
public:
	virtual ~IMaskGenerator() = default;

	virtual bool Start(const RuntimeConfig &config) = 0;
	virtual void Stop() = 0;
	virtual bool SubmitFrame(FrameTicket frame) = 0;
	/* Returns only newly completed mask frames; does not replay cached frames. */
	virtual std::optional<MaskFrame> TryPopMaskFrame() = 0;
	[[nodiscard]] virtual MaskGeneratorStats GetStats() const = 0;
	[[nodiscard]] virtual MaskGeneratorHealth GetHealth() const = 0;
};

class IInstanceTracker {
public:
	virtual ~IInstanceTracker() = default;
	virtual void Update(MaskFrame &frame) = 0;
};

class IRuleCompiler {
public:
	virtual ~IRuleCompiler() = default;
	virtual ExecutionPlan Compile(const std::vector<Rule> &rules,
				      const std::optional<Rule> &default_rule) = 0;
};

class ICompositor {
public:
	virtual ~ICompositor() = default;
	virtual ComposeResult Compose(const ComposeRequest &request, const ExecutionPlan &plan,
				     const MaskFrame *mask_frame) = 0;
};

} // namespace lenses::core
