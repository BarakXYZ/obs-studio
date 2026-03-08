#include "ai/ort/onnx-mask-generator-lifecycle.hpp"

namespace lenses::ai::ort::detail {

void ResetStateForStart(const LifecycleStartContext &context)
{
	if (context.stats)
		*context.stats = {};
	if (context.submit_queue)
		context.submit_queue->clear();
	if (context.output_queue)
		context.output_queue->clear();
	if (context.metrics_collector)
		context.metrics_collector->Reset();
	if (context.stage_budget_last_log_ns)
		*context.stage_budget_last_log_ns = 0;
	if (context.processed_frames)
		*context.processed_frames = 0;
	if (context.submit_similarity_consecutive_skips)
		*context.submit_similarity_consecutive_skips = 0;
	if (context.submit_similarity_prev_sample)
		context.submit_similarity_prev_sample->clear();
	if (context.submit_similarity_scratch_sample)
		context.submit_similarity_scratch_sample->clear();
	if (context.health) {
		*context.health = {};
		context.health->backend = "ort";
		context.health->detail = "Initializing ORT runtime";
	}
}

void SetHealthReady(lenses::core::MaskGeneratorHealth &health,
		    const std::string &active_execution_provider)
{
	health.ready = true;
	health.fallback_active = false;
	health.backend = "ort";
	health.detail = "ORT session ready (provider=" + active_execution_provider + ")";
}

void UpdateCoreMLHealthSnapshot(lenses::core::MaskGeneratorHealth &health, bool coreml_requested,
				bool coreml_enabled, bool coverage_known,
				uint32_t supported_partitions, uint32_t supported_nodes,
				uint32_t total_nodes)
{
	health.coreml_requested = coreml_requested;
	health.coreml_enabled = coreml_enabled;
	health.coreml_coverage_known = coverage_known;
	health.coreml_supported_partitions = supported_partitions;
	health.coreml_supported_nodes = supported_nodes;
	health.coreml_total_nodes = total_nodes;
	health.coreml_coverage_ratio = 0.0f;
	if (coverage_known && total_nodes > 0)
		health.coreml_coverage_ratio = (float)supported_nodes / (float)total_nodes;
	health.cpu_ep_fallback_detected =
		health.coreml_enabled && health.coreml_coverage_known &&
		health.coreml_supported_nodes < health.coreml_total_nodes;
}

void MarkStopping(bool &stop_requested, bool &running)
{
	stop_requested = true;
	running = false;
}

void ResetQueuesAndHistories(const LifecycleStopContext &context)
{
	if (context.submit_queue)
		context.submit_queue->clear();
	if (context.output_queue)
		context.output_queue->clear();
	if (context.metrics_collector)
		context.metrics_collector->Reset();
	if (context.stage_budget_last_log_ns)
		*context.stage_budget_last_log_ns = 0;
	if (context.tracker)
		context.tracker->reset();
}

void ResetHealthForStop(lenses::core::MaskGeneratorHealth &health)
{
	health = {};
	health.backend = "ort";
	health.detail = "ORT runtime stopped";
}

#if defined(LENSES_ENABLE_ORT)
void ResetOrtSessionState(const LifecycleOrtStopContext &context)
{
	if (context.session)
		context.session->reset();
	if (context.env)
		context.env->reset();
	if (context.session_ready)
		*context.session_ready = false;
	if (context.io_binding)
		context.io_binding->reset();
	if (context.io_binding_enabled)
		*context.io_binding_enabled = false;
	if (context.io_binding_static_outputs)
		*context.io_binding_static_outputs = false;
	if (context.io_binding_dynamic_outputs)
		*context.io_binding_dynamic_outputs = false;
	if (context.io_binding_output_memory_info)
		context.io_binding_output_memory_info->reset();
	if (context.bound_detection_shape)
		context.bound_detection_shape->clear();
	if (context.bound_proto_shape)
		context.bound_proto_shape->clear();
	if (context.bound_detection_storage)
		context.bound_detection_storage->clear();
	if (context.bound_proto_storage)
		context.bound_proto_storage->clear();
	if (context.bound_detection_output)
		context.bound_detection_output->reset();
	if (context.bound_proto_output)
		context.bound_proto_output->reset();
	if (context.active_execution_provider)
		*context.active_execution_provider = "cpu";
	if (context.coreml_requested)
		*context.coreml_requested = false;
	if (context.coreml_enabled)
		*context.coreml_enabled = false;
}
#endif

} // namespace lenses::ai::ort::detail
