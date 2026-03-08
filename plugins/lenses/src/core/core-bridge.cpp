#include "lenses/core/core-bridge.h"

#include "lenses/ai/runtime/mask-generator-factory.hpp"
#include "lenses/core/noop-compositor.hpp"
#include "lenses/core/registry.hpp"
#include "lenses/core/rule-compiler.hpp"
#include <obs-module.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace lenses::core {

namespace {

static PreprocessMode ParsePreprocessMode(uint32_t value)
{
	switch (value) {
	case 1:
		return PreprocessMode::Scalar;
	case 2:
		return PreprocessMode::Accelerate;
	default:
		return PreprocessMode::Auto;
	}
}

static SchedulerMode ParseSchedulerMode(uint32_t value)
{
	switch (value) {
	case 1:
		return SchedulerMode::WorkerTimed;
	case 2:
		return SchedulerMode::Adaptive;
	default:
		return SchedulerMode::ProducerTimed;
	}
}

static DropPolicy ParseDropPolicy(uint32_t value)
{
	switch (value) {
	case 1:
		return DropPolicy::DropNewest;
	case 2:
		return DropPolicy::BlockNever;
	default:
		return DropPolicy::DropOldest;
	}
}

} // namespace

class CoreContext final {
public:
	CoreContext() = default;

	bool Start()
	{
		if (!rule_compiler_)
			rule_compiler_ = std::make_unique<DeterministicRuleCompiler>();
		if (!compositor_)
			compositor_ = std::make_unique<NoopCompositor>();

		/*
		 * Runtime configuration depends on filter settings (model/backend/input profile),
		 * which are applied after core creation. Do not eagerly start inference here
		 * with placeholder config, otherwise strict runtime checks can fail before a
		 * real model path is available.
		 */
		runtime_start_error_.clear();

		plan_ = rule_compiler_->Compile({}, std::nullopt);
		drain_warning_emitted_ = false;
		return true;
	}

	void Stop()
	{
		std::scoped_lock lock(runtime_mutex_);
		if (mask_generator_) {
			mask_generator_->Stop();
			mask_generator_.reset();
		}
		registry_.Clear();
		runtime_start_error_.clear();
		drain_warning_emitted_ = false;
	}

	bool SetRuntimeConfig(const RuntimeConfig &config)
	{
		std::scoped_lock lock(runtime_mutex_);
		runtime_config_ = config;
		runtime_start_error_.clear();

		if (mask_generator_) {
			mask_generator_->Stop();
			mask_generator_.reset();
		}

		auto factory_result =
			lenses::ai::runtime::CreateMaskGeneratorWithSelection(runtime_config_);
		runtime_config_ = factory_result.resolved_config;
		if (!factory_result.generator) {
			runtime_start_error_ =
				factory_result.error.empty()
					? std::string("Mask generator factory returned no generator")
					: factory_result.error;
			return false;
		}

		mask_generator_ = std::move(factory_result.generator);
		if (!mask_generator_->Start(runtime_config_)) {
			const auto health = mask_generator_->GetHealth();
			runtime_start_error_ = health.detail.empty()
						 ? std::string("Mask generator failed to start")
						 : health.detail;
			mask_generator_->Stop();
			mask_generator_.reset();
			return false;
		}

		drain_warning_emitted_ = false;
		return true;
	}

	void SubmitFrame(FrameTicket frame)
	{
		std::scoped_lock lock(runtime_mutex_);
		if (!mask_generator_)
			return;

		if (!mask_generator_->SubmitFrame(std::move(frame)))
			return;

		constexpr size_t kMaxDrainIterations = 32;
		std::optional<MaskFrame> latest_mask_frame;
		size_t drain_count = 0;
		for (; drain_count < kMaxDrainIterations; ++drain_count) {
			auto mask_frame = mask_generator_->TryPopMaskFrame();
			if (!mask_frame.has_value())
				break;

			if (latest_mask_frame.has_value() &&
			    latest_mask_frame->frame_id == mask_frame->frame_id &&
			    latest_mask_frame->timestamp_ns == mask_frame->timestamp_ns) {
				if (!drain_warning_emitted_) {
					blog(LOG_WARNING,
					     "[lenses] mask generator repeated frame_id=%" PRIu64
					     " without draining; aborting pop loop",
					     mask_frame->frame_id);
					drain_warning_emitted_ = true;
				}
				latest_mask_frame = std::move(mask_frame);
				break;
			}

			latest_mask_frame = std::move(mask_frame);
		}
		if (drain_count >= kMaxDrainIterations && !drain_warning_emitted_) {
			blog(LOG_WARNING, "[lenses] mask generator exceeded max drain iterations (%zu)",
			     kMaxDrainIterations);
			drain_warning_emitted_ = true;
		}
		if (latest_mask_frame.has_value())
			drain_warning_emitted_ = false;

		if (!latest_mask_frame.has_value()) {
			if (!runtime_config_.fallback_to_last_mask)
				registry_.Clear();
			return;
		}

		MaskFrame frame_for_store = std::move(*latest_mask_frame);
		ComposeRequest request{};
		request.frame_id = frame_for_store.frame_id;
		request.source_width = frame_for_store.source_width;
		request.source_height = frame_for_store.source_height;
		(void)compositor_->Compose(request, plan_, &frame_for_store);
		registry_.Store(std::move(frame_for_store));
	}

	bool TryGetLatestMaskFrameId(uint64_t *frame_id_out)
	{
		if (!frame_id_out)
			return false;

		auto latest = registry_.Latest();
		if (!latest)
			return false;

		*frame_id_out = latest->frame_id;
		return true;
	}

	bool GetLatestMaskFrameInfo(struct lenses_core_mask_frame_info *out_info)
	{
		if (!out_info)
			return false;

		const auto latest = registry_.Latest();
		if (!latest)
			return false;

		out_info->frame_id = latest->frame_id;
		out_info->source_width = latest->source_width;
		out_info->source_height = latest->source_height;
		out_info->timestamp_ns = latest->timestamp_ns;
		out_info->instance_count = latest->instances.size();
		out_info->class_mask_count = latest->class_union_masks.size();
		return true;
	}

	size_t CopyLatestMaskInstances(struct lenses_core_mask_instance *out_instances,
				       size_t max_instances)
	{
		const auto latest = registry_.Latest();
		if (!latest || !out_instances || max_instances == 0)
			return 0;

		const size_t count = std::min(max_instances, latest->instances.size());
		for (size_t i = 0; i < count; ++i) {
			const auto &src = latest->instances[i];
			auto &dst = out_instances[i];
			dst.track_id = src.track_id;
			dst.class_id = src.class_id;
			dst.confidence = src.confidence;
			dst.bbox_x = src.bbox_norm.x;
			dst.bbox_y = src.bbox_norm.y;
			dst.bbox_width = src.bbox_norm.width;
			dst.bbox_height = src.bbox_norm.height;
			dst.mask_handle = src.mask_handle.value;
			dst.timestamp_ns = src.timestamp_ns;
		}

		return count;
	}

	size_t CopyLatestClassMasks(struct lenses_core_class_mask *out_class_masks,
				    size_t max_class_masks)
	{
		const auto latest = registry_.Latest();
		if (!latest || !out_class_masks || max_class_masks == 0)
			return 0;

		size_t i = 0;
		for (const auto &[class_id, handle] : latest->class_union_masks) {
			if (i >= max_class_masks)
				break;
			out_class_masks[i].class_id = class_id;
			out_class_masks[i].mask_handle = handle.value;
			++i;
		}
		return i;
	}

	bool CopyMaskBitmap(uint64_t mask_handle, uint8_t *out_data, size_t out_capacity,
			    uint32_t *out_width, uint32_t *out_height, size_t *out_required_bytes)
	{
		if (mask_handle == 0)
			return false;

		const auto latest = registry_.Latest();
		if (!latest)
			return false;

		const auto it = latest->mask_bitmaps.find(mask_handle);
		if (it == latest->mask_bitmaps.end())
			return false;

		const auto &mask = it->second;
		const size_t required = (size_t)mask.width * (size_t)mask.height;
		if (out_width)
			*out_width = mask.width;
		if (out_height)
			*out_height = mask.height;
		if (out_required_bytes)
			*out_required_bytes = required;

		if (!out_data)
			return true;
		if (out_capacity < required || mask.data.size() < required)
			return false;

		memcpy(out_data, mask.data.data(), required);
		return true;
	}

	[[nodiscard]] MaskGeneratorStats GetStats() const
	{
		std::scoped_lock lock(runtime_mutex_);
		if (!mask_generator_)
			return {};
		return mask_generator_->GetStats();
	}

	[[nodiscard]] MaskGeneratorHealth GetHealth() const
	{
		std::scoped_lock lock(runtime_mutex_);
		if (!mask_generator_) {
			MaskGeneratorHealth health{};
			health.ready = false;
			health.fallback_active = false;
			health.backend = "none";
			health.detail = runtime_start_error_.empty()
					 ? "Mask generator is not initialized"
					 : runtime_start_error_;
			return health;
		}
		return mask_generator_->GetHealth();
	}

private:
	mutable std::mutex runtime_mutex_;
	MaskRegistry registry_{};
	RuntimeConfig runtime_config_{};
	std::unique_ptr<IMaskGenerator> mask_generator_{};
	std::unique_ptr<IRuleCompiler> rule_compiler_{};
	std::unique_ptr<ICompositor> compositor_{};
	ExecutionPlan plan_{};
	std::string runtime_start_error_{};
	bool drain_warning_emitted_ = false;
};

} // namespace lenses::core

struct lenses_core_context {
	std::unique_ptr<lenses::core::CoreContext> impl;
};

static void lenses_fill_runtime_stats(const lenses::core::MaskGeneratorStats &stats,
				      struct lenses_core_runtime_stats *runtime_stats)
{
	if (!runtime_stats)
		return;

	runtime_stats->submitted_frames = stats.submitted_frames;
	runtime_stats->completed_frames = stats.completed_frames;
	runtime_stats->dropped_frames = stats.dropped_frames;
	runtime_stats->cadence_skipped_frames = stats.cadence_skipped_frames;
	runtime_stats->similarity_skipped_frames = stats.similarity_skipped_frames;
	runtime_stats->reused_last_mask_frames = stats.reused_last_mask_frames;
	runtime_stats->cloud_timeout_frames = stats.cloud_timeout_frames;
	runtime_stats->cloud_fallback_frames = stats.cloud_fallback_frames;
	runtime_stats->last_latency_ms = stats.last_latency_ms;
	runtime_stats->last_readback_ms = stats.last_readback_ms;
	runtime_stats->last_preprocess_ms = stats.last_preprocess_ms;
	runtime_stats->last_infer_ms = stats.last_infer_ms;
	runtime_stats->last_decode_ms = stats.last_decode_ms;
	runtime_stats->last_track_ms = stats.last_track_ms;
	runtime_stats->last_queue_latency_ms = stats.last_queue_latency_ms;
	runtime_stats->last_end_to_end_latency_ms = stats.last_end_to_end_latency_ms;
	runtime_stats->submit_fps = stats.submit_fps;
	runtime_stats->complete_fps = stats.complete_fps;
	runtime_stats->drop_fps = stats.drop_fps;
	runtime_stats->readback_ms_p50 = stats.readback_ms_p50;
	runtime_stats->readback_ms_p95 = stats.readback_ms_p95;
	runtime_stats->readback_ms_p99 = stats.readback_ms_p99;
	runtime_stats->preprocess_ms_p50 = stats.preprocess_ms_p50;
	runtime_stats->preprocess_ms_p95 = stats.preprocess_ms_p95;
	runtime_stats->preprocess_ms_p99 = stats.preprocess_ms_p99;
	runtime_stats->infer_ms_p50 = stats.infer_ms_p50;
	runtime_stats->infer_ms_p95 = stats.infer_ms_p95;
	runtime_stats->infer_ms_p99 = stats.infer_ms_p99;
	runtime_stats->decode_ms_p50 = stats.decode_ms_p50;
	runtime_stats->decode_ms_p95 = stats.decode_ms_p95;
	runtime_stats->decode_ms_p99 = stats.decode_ms_p99;
	runtime_stats->track_ms_p50 = stats.track_ms_p50;
	runtime_stats->track_ms_p95 = stats.track_ms_p95;
	runtime_stats->track_ms_p99 = stats.track_ms_p99;
	runtime_stats->queue_latency_ms_p50 = stats.queue_latency_ms_p50;
	runtime_stats->queue_latency_ms_p95 = stats.queue_latency_ms_p95;
	runtime_stats->queue_latency_ms_p99 = stats.queue_latency_ms_p99;
	runtime_stats->end_to_end_latency_ms_p50 = stats.end_to_end_latency_ms_p50;
	runtime_stats->end_to_end_latency_ms_p95 = stats.end_to_end_latency_ms_p95;
	runtime_stats->end_to_end_latency_ms_p99 = stats.end_to_end_latency_ms_p99;
	runtime_stats->submit_queue_depth = stats.submit_queue_depth;
	runtime_stats->output_queue_depth = stats.output_queue_depth;
}

static void lenses_fill_runtime_health(const lenses::core::MaskGeneratorHealth &health,
				       struct lenses_core_runtime_health *out_health)
{
	if (!out_health)
		return;

	out_health->ready = health.ready;
	out_health->fallback_active = health.fallback_active;
	out_health->coreml_requested = health.coreml_requested;
	out_health->coreml_enabled = health.coreml_enabled;
	out_health->coreml_coverage_known = health.coreml_coverage_known;
	out_health->cpu_ep_fallback_detected = health.cpu_ep_fallback_detected;
	out_health->cpu_ep_fallback_disabled = health.cpu_ep_fallback_disabled;
	out_health->coreml_supported_partitions = health.coreml_supported_partitions;
	out_health->coreml_supported_nodes = health.coreml_supported_nodes;
	out_health->coreml_total_nodes = health.coreml_total_nodes;
	out_health->coreml_coverage_ratio = health.coreml_coverage_ratio;
	snprintf(out_health->backend, sizeof(out_health->backend), "%s", health.backend.c_str());
	snprintf(out_health->detail, sizeof(out_health->detail), "%s", health.detail.c_str());
}

struct lenses_core_context *lenses_core_create(void)
{
	auto *context = new lenses_core_context{std::make_unique<lenses::core::CoreContext>()};
	if (!context->impl->Start()) {
		delete context;
		return nullptr;
	}
	return context;
}

void lenses_core_destroy(struct lenses_core_context **context)
{
	if (!context || !*context)
		return;

	(*context)->impl->Stop();
	delete *context;
	*context = nullptr;
}

void lenses_core_set_runtime_config(struct lenses_core_context *context,
				    const struct lenses_core_runtime_config *config)
{
	if (!context || !config)
		return;

	lenses::core::RuntimeConfig runtime_config{};
	runtime_config.ai_fps_target = config->ai_fps_target;
	runtime_config.input_width = config->input_width;
	runtime_config.input_height = config->input_height;
	runtime_config.inference_every_n_frames = config->inference_every_n_frames;
	runtime_config.enable_similarity_skip = config->enable_similarity_skip;
	runtime_config.similarity_threshold = config->similarity_threshold;
	runtime_config.cpu_intra_op_threads = config->cpu_intra_op_threads;
	runtime_config.cpu_inter_op_threads = config->cpu_inter_op_threads;
	runtime_config.enable_iobinding = config->enable_iobinding;
	runtime_config.submit_queue_limit = config->submit_queue_limit;
	runtime_config.output_queue_limit = config->output_queue_limit;
	runtime_config.strict_runtime_checks = config->strict_runtime_checks;
	runtime_config.fallback_to_last_mask = config->fallback_to_last_mask;
	runtime_config.preprocess_mode =
		lenses::core::ParsePreprocessMode(config->preprocess_mode);
	runtime_config.scheduler_mode =
		lenses::core::ParseSchedulerMode(config->scheduler_mode);
	runtime_config.drop_policy = lenses::core::ParseDropPolicy(config->drop_policy);
	runtime_config.profiling_enabled = config->profiling_enabled;
	runtime_config.stage_budget_ms = config->stage_budget_ms;
	if (config->provider)
		runtime_config.provider = config->provider;
	if (config->execution_provider)
		runtime_config.execution_provider = config->execution_provider;
	if (config->model_path)
		runtime_config.model_path = config->model_path;
	if (config->cloud_endpoint)
		runtime_config.cloud_endpoint = config->cloud_endpoint;
	runtime_config.cloud_timeout_ms = config->cloud_timeout_ms;
	runtime_config.model_dynamic_shape = config->model_dynamic_shape;
	runtime_config.model_static_input = config->model_static_input;
	runtime_config.model_static_output = config->model_static_output;
	runtime_config.model_supports_iobinding_static_outputs =
		config->model_supports_iobinding_static_outputs;

	(void)context->impl->SetRuntimeConfig(runtime_config);
}

void lenses_core_submit_frame(struct lenses_core_context *context, uint64_t frame_id,
			      uint32_t source_width, uint32_t source_height,
			      uint64_t timestamp_ns)
{
	if (!context)
		return;

	lenses::core::FrameTicket frame{};
	frame.frame_id = frame_id;
	frame.source_width = source_width;
	frame.source_height = source_height;
	frame.timestamp_ns = timestamp_ns;
	context->impl->SubmitFrame(std::move(frame));
}

void lenses_core_submit_frame_bgra(struct lenses_core_context *context, uint64_t frame_id,
				   uint32_t source_width, uint32_t source_height,
				   uint64_t timestamp_ns, uint32_t image_width,
				   uint32_t image_height, uint32_t image_linesize,
				   const uint8_t *image_bgra, size_t image_size,
				   double readback_ms)
{
	if (!context)
		return;

	lenses::core::FrameTicket frame{};
	frame.frame_id = frame_id;
	frame.source_width = source_width;
	frame.source_height = source_height;
	frame.timestamp_ns = timestamp_ns;
	frame.image_width = image_width;
	frame.image_height = image_height;
	frame.image_linesize = image_linesize;
	frame.readback_ms = readback_ms;
	if (image_bgra && image_size > 0)
		frame.image_bgra.assign(image_bgra, image_bgra + image_size);
	context->impl->SubmitFrame(std::move(frame));
}

bool lenses_core_try_get_latest_mask_frame_id(struct lenses_core_context *context,
				      uint64_t *frame_id_out)
{
	if (!context)
		return false;

	return context->impl->TryGetLatestMaskFrameId(frame_id_out);
}

bool lenses_core_get_latest_mask_frame_info(struct lenses_core_context *context,
					    struct lenses_core_mask_frame_info *out_info)
{
	if (!context)
		return false;

	return context->impl->GetLatestMaskFrameInfo(out_info);
}

size_t lenses_core_copy_latest_mask_instances(struct lenses_core_context *context,
					      struct lenses_core_mask_instance *out_instances,
					      size_t max_instances)
{
	if (!context)
		return 0;

	return context->impl->CopyLatestMaskInstances(out_instances, max_instances);
}

size_t lenses_core_copy_latest_class_masks(struct lenses_core_context *context,
					   struct lenses_core_class_mask *out_class_masks,
					   size_t max_class_masks)
{
	if (!context)
		return 0;

	return context->impl->CopyLatestClassMasks(out_class_masks, max_class_masks);
}

bool lenses_core_copy_mask_bitmap(struct lenses_core_context *context, uint64_t mask_handle,
				  uint8_t *out_data, size_t out_capacity,
				  uint32_t *out_width, uint32_t *out_height,
				  size_t *out_required_bytes)
{
	if (!context)
		return false;

	return context->impl->CopyMaskBitmap(mask_handle, out_data, out_capacity, out_width, out_height,
					     out_required_bytes);
}

struct lenses_core_runtime_stats lenses_core_get_runtime_stats(const struct lenses_core_context *context)
{
	struct lenses_core_runtime_stats runtime_stats{};
	if (!context)
		return runtime_stats;

	const lenses::core::MaskGeneratorStats stats = context->impl->GetStats();
	lenses_fill_runtime_stats(stats, &runtime_stats);
	return runtime_stats;
}

bool lenses_core_get_runtime_health(const struct lenses_core_context *context,
				    struct lenses_core_runtime_health *out_health)
{
	if (!context || !out_health)
		return false;

	const lenses::core::MaskGeneratorHealth health = context->impl->GetHealth();
	lenses_fill_runtime_health(health, out_health);
	return true;
}

bool lenses_core_get_runtime_snapshot(const struct lenses_core_context *context,
				      struct lenses_core_runtime_snapshot *out_snapshot)
{
	if (!context || !out_snapshot)
		return false;

	memset(out_snapshot, 0, sizeof(*out_snapshot));

	const lenses::core::MaskGeneratorStats stats = context->impl->GetStats();
	const lenses::core::MaskGeneratorHealth health = context->impl->GetHealth();
	lenses_fill_runtime_stats(stats, &out_snapshot->stats);
	lenses_fill_runtime_health(health, &out_snapshot->health);
	out_snapshot->has_health = true;
	out_snapshot->has_latest_mask_frame =
		context->impl->GetLatestMaskFrameInfo(&out_snapshot->latest_mask_frame);
	return true;
}
