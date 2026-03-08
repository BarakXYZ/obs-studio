#pragma once

#include "ai/ort/onnx-mask-generator-stats.hpp"

#include "lenses/core/interfaces.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace lenses::ai::ort::detail {

struct WorkerStageMetrics {
	double readback_ms = 0.0;
	double preprocess_ms = 0.0;
	double infer_ms = 0.0;
	double decode_ms = 0.0;
	double track_ms = 0.0;
	double queue_latency_ms = 0.0;
	double end_to_end_latency_ms = 0.0;
};

struct WorkerPublishContext {
	size_t output_queue_limit = 1;
	std::deque<lenses::core::MaskFrame> *output_queue = nullptr;
	lenses::core::MaskGeneratorStats *stats = nullptr;
	std::mutex *mutex = nullptr;
	RuntimeMetricsCollector *metrics_collector = nullptr;
};

struct WorkerFailureContext {
	uint32_t input_width = 0;
	uint32_t input_height = 0;
	uint32_t proto_width = 0;
	uint32_t proto_height = 0;
	uint32_t mask_dim = 0;
	size_t detection_output_index = 0;
	size_t proto_output_index = 0;
};

struct WorkerContextState {
	const lenses::core::RuntimeConfig *config = nullptr;
	std::deque<lenses::core::MaskFrame> *output_queue = nullptr;
	lenses::core::MaskGeneratorStats *stats = nullptr;
	std::mutex *mutex = nullptr;

	const uint32_t *input_width = nullptr;
	const uint32_t *input_height = nullptr;
	const uint32_t *proto_width = nullptr;
	const uint32_t *proto_height = nullptr;
	const uint32_t *mask_dim = nullptr;
	const size_t *detection_output_index = nullptr;
	const size_t *proto_output_index = nullptr;
	RuntimeMetricsCollector *metrics_collector = nullptr;
};

using WorkerInferenceCallback =
	std::function<bool(const lenses::core::FrameTicket &, lenses::core::MaskFrame &,
			   WorkerStageMetrics &, bool &, const char *&)>;

struct WorkerLoopContext {
	const lenses::core::RuntimeConfig *config = nullptr;
	std::mutex *mutex = nullptr;
	std::condition_variable *cv = nullptr;
	std::deque<lenses::core::FrameTicket> *submit_queue = nullptr;
	std::deque<lenses::core::MaskFrame> *output_queue = nullptr;
	lenses::core::MaskGeneratorStats *stats = nullptr;
	lenses::core::MaskGeneratorHealth *health = nullptr;
	RuntimeMetricsCollector *metrics_collector = nullptr;
	bool *stop_requested = nullptr;
	uint64_t *processed_frames = nullptr;
	uint64_t *stage_budget_last_log_ns = nullptr;
	WorkerContextState worker_state{};
	WorkerInferenceCallback run_inference;
};

WorkerFailureContext BuildWorkerFailureContext(const WorkerContextState &state);

WorkerPublishContext BuildWorkerPublishContext(const WorkerContextState &state);

bool ShouldLogConsecutiveFailure(uint64_t consecutive_failures);

void HandleFailureState(bool failed_with_exception, const char *failure_reason,
			uint64_t consecutive_failures, bool &logged_once,
			const WorkerFailureContext &context);

void UpdateHealthOnFailure(bool failed_with_exception, const char *failure_reason,
			   lenses::core::MaskGeneratorHealth &health, std::mutex &mutex);

void RecordCadenceSkip(double readback_ms, bool worker_timed,
		       const std::chrono::milliseconds &min_interval,
		       lenses::core::MaskGeneratorStats &stats, std::mutex &mutex);

void PublishStageMetrics(bool ok, bool failed_with_exception,
			 const WorkerStageMetrics &stage_metrics,
			 lenses::core::MaskFrame mask_frame,
			 const WorkerPublishContext &context);

void LogStageBudgetExceeded(const WorkerStageMetrics &stage_metrics,
			    double stage_budget_ms, uint64_t &stage_budget_last_log_ns);

void MaybeSleepWorker(bool worker_timed,
		      const std::chrono::steady_clock::duration elapsed,
		      const std::chrono::milliseconds &min_interval);

void RunWorkerLoop(WorkerLoopContext &context);

} // namespace lenses::ai::ort::detail
