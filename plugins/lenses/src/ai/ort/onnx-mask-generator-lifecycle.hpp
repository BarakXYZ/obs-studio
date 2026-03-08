#pragma once

#include "ai/ort/onnx-mask-generator-stats.hpp"

#include "lenses/ai/tracking/bytetrack-tracker.hpp"
#include "lenses/core/interfaces.hpp"

#if defined(LENSES_ENABLE_ORT)
#include <onnxruntime_cxx_api.h>
#endif

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace lenses::ai::ort::detail {

struct LifecycleStartContext {
	lenses::core::MaskGeneratorStats *stats = nullptr;
	std::deque<lenses::core::FrameTicket> *submit_queue = nullptr;
	std::deque<lenses::core::MaskFrame> *output_queue = nullptr;
	RuntimeMetricsCollector *metrics_collector = nullptr;
	uint64_t *stage_budget_last_log_ns = nullptr;
	uint64_t *processed_frames = nullptr;
	uint32_t *submit_similarity_consecutive_skips = nullptr;
	std::vector<uint8_t> *submit_similarity_prev_sample = nullptr;
	std::vector<uint8_t> *submit_similarity_scratch_sample = nullptr;
	lenses::core::MaskGeneratorHealth *health = nullptr;
};

void ResetStateForStart(const LifecycleStartContext &context);
void SetHealthReady(lenses::core::MaskGeneratorHealth &health,
		    const std::string &active_execution_provider);
void UpdateCoreMLHealthSnapshot(lenses::core::MaskGeneratorHealth &health, bool coreml_requested,
				bool coreml_enabled, bool coverage_known,
				uint32_t supported_partitions, uint32_t supported_nodes,
				uint32_t total_nodes);

struct LifecycleStopContext {
	std::deque<lenses::core::FrameTicket> *submit_queue = nullptr;
	std::deque<lenses::core::MaskFrame> *output_queue = nullptr;
	RuntimeMetricsCollector *metrics_collector = nullptr;
	uint64_t *stage_budget_last_log_ns = nullptr;
	std::unique_ptr<lenses::ai::tracking::ByteTrackTracker> *tracker = nullptr;
};

void MarkStopping(bool &stop_requested, bool &running);
void ResetQueuesAndHistories(const LifecycleStopContext &context);
void ResetHealthForStop(lenses::core::MaskGeneratorHealth &health);

#if defined(LENSES_ENABLE_ORT)
struct LifecycleOrtStopContext {
	std::unique_ptr<Ort::Session> *session = nullptr;
	std::unique_ptr<Ort::Env> *env = nullptr;
	bool *session_ready = nullptr;
	std::unique_ptr<Ort::IoBinding> *io_binding = nullptr;
	bool *io_binding_enabled = nullptr;
	bool *io_binding_static_outputs = nullptr;
	bool *io_binding_dynamic_outputs = nullptr;
	std::unique_ptr<Ort::MemoryInfo> *io_binding_output_memory_info = nullptr;
	std::vector<int64_t> *bound_detection_shape = nullptr;
	std::vector<int64_t> *bound_proto_shape = nullptr;
	std::vector<float> *bound_detection_storage = nullptr;
	std::vector<float> *bound_proto_storage = nullptr;
	std::unique_ptr<Ort::Value> *bound_detection_output = nullptr;
	std::unique_ptr<Ort::Value> *bound_proto_output = nullptr;
	std::string *active_execution_provider = nullptr;
	bool *coreml_requested = nullptr;
	bool *coreml_enabled = nullptr;
};

void ResetOrtSessionState(const LifecycleOrtStopContext &context);
#endif

} // namespace lenses::ai::ort::detail
