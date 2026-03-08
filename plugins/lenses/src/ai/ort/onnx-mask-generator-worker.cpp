#include "ai/ort/onnx-mask-generator-worker.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <inttypes.h>
#include <string>
#include <thread>
#include <utility>

namespace lenses::ai::ort::detail {

bool ShouldLogConsecutiveFailure(uint64_t consecutive_failures)
{
	return consecutive_failures == 1 || consecutive_failures % 120 == 0;
}

WorkerFailureContext BuildWorkerFailureContext(const WorkerContextState &state)
{
	WorkerFailureContext context{};
	context.input_width = state.input_width ? *state.input_width : 0;
	context.input_height = state.input_height ? *state.input_height : 0;
	context.proto_width = state.proto_width ? *state.proto_width : 0;
	context.proto_height = state.proto_height ? *state.proto_height : 0;
	context.mask_dim = state.mask_dim ? *state.mask_dim : 0;
	context.detection_output_index =
		state.detection_output_index ? *state.detection_output_index : 0;
	context.proto_output_index = state.proto_output_index ? *state.proto_output_index : 0;
	return context;
}

WorkerPublishContext BuildWorkerPublishContext(const WorkerContextState &state)
{
	WorkerPublishContext context{};
	context.output_queue_limit = std::max<size_t>(
		1, state.config ? state.config->output_queue_limit : 1);
	context.output_queue = state.output_queue;
	context.stats = state.stats;
	context.mutex = state.mutex;
	context.metrics_collector = state.metrics_collector;
	return context;
}

void HandleFailureState(bool failed_with_exception, const char *failure_reason,
			uint64_t consecutive_failures, bool &logged_once,
			const WorkerFailureContext &context)
{
	if (!ShouldLogConsecutiveFailure(consecutive_failures))
		return;

	if (!failed_with_exception) {
		blog(LOG_WARNING,
		     "[lenses] ORT inference returned no mask frame, consecutive_failures=%" PRIu64
		     " input=%ux%u proto=%ux%u mask_dim=%u det_idx=%zu proto_idx=%zu",
		     consecutive_failures, context.input_width, context.input_height,
		     context.proto_width, context.proto_height, context.mask_dim,
		     context.detection_output_index, context.proto_output_index);
		return;
	}

	blog(LOG_WARNING,
	     "[lenses] ORT inference loop exception (%s), consecutive_failures=%" PRIu64,
	     failure_reason ? failure_reason : "unknown", consecutive_failures);
	logged_once = true;
}

void UpdateHealthOnFailure(bool failed_with_exception, const char *failure_reason,
			   lenses::core::MaskGeneratorHealth &health, std::mutex &mutex)
{
	if (!failed_with_exception)
		return;

	std::scoped_lock lock(mutex);
	health.ready = false;
	health.fallback_active = false;
	health.backend = "ort";
	health.detail =
		std::string("ORT runtime error: ") + (failure_reason ? failure_reason : "unknown");
}

void RecordCadenceSkip(double readback_ms, bool worker_timed,
		       const std::chrono::milliseconds &min_interval,
		       lenses::core::MaskGeneratorStats &stats, std::mutex &mutex)
{
	{
		std::scoped_lock lock(mutex);
		stats.cadence_skipped_frames++;
		stats.reused_last_mask_frames++;
		stats.last_readback_ms = readback_ms;
	}
	if (worker_timed && min_interval.count() > 0)
		std::this_thread::sleep_for(min_interval);
}

void PublishStageMetrics(bool ok, bool failed_with_exception,
			 const WorkerStageMetrics &stage_metrics,
			 lenses::core::MaskFrame mask_frame,
			 const WorkerPublishContext &context)
{
	if (!context.output_queue || !context.stats || !context.mutex)
		return;

	std::scoped_lock lock(*context.mutex);
	const uint64_t now_ns = os_gettime_ns();
	if (ok) {
		const size_t output_limit = std::max<size_t>(1, context.output_queue_limit);
		if (context.output_queue->size() >= output_limit) {
			context.output_queue->pop_front();
			context.stats->dropped_frames++;
			if (context.metrics_collector)
				context.metrics_collector->RecordDropEvent(now_ns);
		}

		context.output_queue->push_back(std::move(mask_frame));
		context.stats->completed_frames++;
		if (context.metrics_collector)
			context.metrics_collector->RecordCompleteEvent(now_ns);
		context.stats->last_latency_ms = stage_metrics.end_to_end_latency_ms;
		context.stats->last_readback_ms = stage_metrics.readback_ms;
		context.stats->last_preprocess_ms = stage_metrics.preprocess_ms;
		context.stats->last_infer_ms = stage_metrics.infer_ms;
		context.stats->last_decode_ms = stage_metrics.decode_ms;
		context.stats->last_track_ms = stage_metrics.track_ms;
		context.stats->last_queue_latency_ms = stage_metrics.queue_latency_ms;
		context.stats->last_end_to_end_latency_ms = stage_metrics.end_to_end_latency_ms;
		if (context.metrics_collector) {
			context.metrics_collector->RecordStageTimings(
				stage_metrics.readback_ms, stage_metrics.preprocess_ms,
				stage_metrics.infer_ms, stage_metrics.decode_ms,
				stage_metrics.track_ms);
			context.metrics_collector->RecordQueueLatency(
				stage_metrics.queue_latency_ms);
			context.metrics_collector->RecordEndToEndLatency(
				stage_metrics.end_to_end_latency_ms);
		}
		context.stats->output_queue_depth = context.output_queue->size();
		return;
	}

	if (!failed_with_exception) {
		context.stats->dropped_frames++;
		if (context.metrics_collector)
			context.metrics_collector->RecordDropEvent(now_ns);
		context.stats->last_readback_ms = stage_metrics.readback_ms;
		context.stats->last_preprocess_ms = stage_metrics.preprocess_ms;
		context.stats->last_infer_ms = stage_metrics.infer_ms;
		context.stats->last_decode_ms = stage_metrics.decode_ms;
		context.stats->last_track_ms = stage_metrics.track_ms;
		context.stats->last_queue_latency_ms = stage_metrics.queue_latency_ms;
		context.stats->last_end_to_end_latency_ms = stage_metrics.end_to_end_latency_ms;
	}
}

void LogStageBudgetExceeded(const WorkerStageMetrics &stage_metrics,
			    double stage_budget_ms, uint64_t &stage_budget_last_log_ns)
{
	if (stage_budget_ms <= 0.0)
		return;

	const double max_stage = std::max(
		std::max(stage_metrics.readback_ms, stage_metrics.preprocess_ms),
		std::max(std::max(stage_metrics.infer_ms, stage_metrics.decode_ms),
			 stage_metrics.track_ms));
	const uint64_t now_ns = os_gettime_ns();
	if (max_stage > stage_budget_ms &&
	    now_ns - stage_budget_last_log_ns > 2000000000ULL) {
		blog(LOG_WARNING,
		     "[lenses] stage budget exceeded budget=%.2fms readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f",
		     stage_budget_ms, stage_metrics.readback_ms, stage_metrics.preprocess_ms,
		     stage_metrics.infer_ms, stage_metrics.decode_ms, stage_metrics.track_ms);
		stage_budget_last_log_ns = now_ns;
	}
}

void MaybeSleepWorker(bool worker_timed,
		      const std::chrono::steady_clock::duration elapsed,
		      const std::chrono::milliseconds &min_interval)
{
	if (worker_timed && elapsed < min_interval)
		std::this_thread::sleep_for(min_interval - elapsed);
}

void RunWorkerLoop(WorkerLoopContext &context)
{
	if (!context.config || !context.mutex || !context.cv || !context.submit_queue ||
	    !context.stats || !context.stop_requested || !context.processed_frames ||
	    !context.stage_budget_last_log_ns || !context.run_inference) {
		return;
	}

	WorkerContextState worker_state = context.worker_state;
	if (!worker_state.config)
		worker_state.config = context.config;
	if (!worker_state.output_queue)
		worker_state.output_queue = context.output_queue;
	if (!worker_state.stats)
		worker_state.stats = context.stats;
	if (!worker_state.mutex)
		worker_state.mutex = context.mutex;
	if (!worker_state.metrics_collector)
		worker_state.metrics_collector = context.metrics_collector;

	const uint32_t fps = std::max<uint32_t>(1, context.config->ai_fps_target);
	const auto min_interval = std::chrono::milliseconds(1000 / fps);
	const bool worker_timed =
		context.config->scheduler_mode == lenses::core::SchedulerMode::WorkerTimed;
	const uint32_t infer_every_n =
		std::max<uint32_t>(1, context.config->inference_every_n_frames);
	uint64_t consecutive_failures = 0;

	for (;;) {
		lenses::core::FrameTicket frame{};
		{
			std::unique_lock lock(*context.mutex);
			context.cv->wait(lock, [&] {
				return *context.stop_requested || !context.submit_queue->empty();
			});
			if (*context.stop_requested)
				return;

			frame = std::move(context.submit_queue->front());
			context.submit_queue->pop_front();
			context.stats->submit_queue_depth = context.submit_queue->size();
		}

		const auto started = std::chrono::steady_clock::now();
		const uint64_t dequeued_ns = os_gettime_ns();
		WorkerStageMetrics stage_metrics{};
		stage_metrics.readback_ms = frame.readback_ms;
		if (frame.runtime_submit_ns > 0 && dequeued_ns >= frame.runtime_submit_ns)
			stage_metrics.queue_latency_ms =
				(double)(dequeued_ns - frame.runtime_submit_ns) / 1000000.0;

		const uint64_t frame_index = (*context.processed_frames)++;
		const bool cadence_skip =
			infer_every_n > 1 && ((frame_index % infer_every_n) != 0);
		if (cadence_skip) {
			RecordCadenceSkip(frame.readback_ms, worker_timed, min_interval,
					 *context.stats, *context.mutex);
			continue;
		}

		lenses::core::MaskFrame mask_frame{};
		bool ok = false;
		const char *failure_reason = nullptr;
		bool failed_with_exception = false;
		bool logged_once = false;
		ok = context.run_inference(frame, mask_frame, stage_metrics,
					   failed_with_exception, failure_reason);
		if (ok) {
			mask_frame.latency_ms = std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now() - started)
							.count();
		}

		const uint64_t finished_ns = os_gettime_ns();
		if (frame.runtime_submit_ns > 0 && finished_ns >= frame.runtime_submit_ns) {
			stage_metrics.end_to_end_latency_ms =
				(double)(finished_ns - frame.runtime_submit_ns) / 1000000.0;
		} else {
			stage_metrics.end_to_end_latency_ms = mask_frame.latency_ms;
		}
		mask_frame.latency_ms = stage_metrics.end_to_end_latency_ms;

		if (ok) {
			consecutive_failures = 0;
		} else {
			consecutive_failures++;
			HandleFailureState(failed_with_exception, failure_reason,
					   consecutive_failures, logged_once,
					   BuildWorkerFailureContext(worker_state));
			if (context.health) {
				UpdateHealthOnFailure(failed_with_exception, failure_reason,
						     *context.health, *context.mutex);
			}
		}

		PublishStageMetrics(ok, failed_with_exception, stage_metrics,
				    std::move(mask_frame),
				    BuildWorkerPublishContext(worker_state));
		LogStageBudgetExceeded(stage_metrics, context.config->stage_budget_ms,
				       *context.stage_budget_last_log_ns);

		if (failed_with_exception && !logged_once &&
		    ShouldLogConsecutiveFailure(consecutive_failures)) {
			blog(LOG_WARNING,
			     "[lenses] ORT inference loop exception, consecutive_failures=%" PRIu64,
			     consecutive_failures);
		}

		const auto elapsed = std::chrono::steady_clock::now() - started;
		MaybeSleepWorker(worker_timed, elapsed, min_interval);
	}
}

} // namespace lenses::ai::ort::detail
