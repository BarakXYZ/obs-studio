#include "ai/ort/onnx-mask-generator.hpp"
#include "ai/ort/onnx-mask-generator-artifacts.hpp"
#include "ai/ort/onnx-mask-generator-bootstrap.hpp"
#include "ai/ort/onnx-mask-generator-iobinding.hpp"
#include "ai/ort/onnx-mask-generator-layout.hpp"
#include "ai/ort/onnx-mask-generator-lifecycle.hpp"
#include "ai/ort/onnx-mask-generator-preprocess.hpp"
#include "ai/ort/onnx-mask-generator-provider.hpp"
#include "ai/ort/onnx-mask-generator-runner.hpp"
#include "ai/ort/onnx-mask-generator-session-io.hpp"
#include "ai/ort/onnx-mask-generator-submit.hpp"
#include "ai/ort/onnx-mask-generator-stats.hpp"
#include "ai/ort/onnx-mask-generator-worker.hpp"

#include "lenses/ai/tracking/bytetrack-tracker.hpp"
#include <obs-module.h>
#include <util/platform.h>

#if defined(LENSES_ENABLE_ORT)
#include <onnxruntime_cxx_api.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <inttypes.h>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lenses::ai::ort {

namespace {

#if defined(LENSES_ENABLE_ORT)
constexpr float kDefaultConfidenceThreshold = 0.25f;
constexpr float kDefaultMaskThreshold = 0.5f;
constexpr float kNmsIouThreshold = 0.5f;
constexpr size_t kMaxDetections = 200;
constexpr uint32_t kMaxInputDimension = 4096;
constexpr uint32_t kMaxProtoDimension = 1024;
constexpr size_t kMaxDetectionCandidates = 120000;
constexpr size_t kMaxDetectionFeatures = 2048;
constexpr uint32_t kDefaultCocoClassCount = 80;

#endif

} // namespace

class OnnxMaskGenerator::Impl final {
public:
	Impl() = default;

	bool Start(const lenses::core::RuntimeConfig &runtime_config)
	{
		Stop();
		config = runtime_config;
		detail::LifecycleStartContext start_context{};
		start_context.stats = &stats;
		start_context.submit_queue = &submit_queue;
		start_context.output_queue = &output_queue;
		start_context.metrics_collector = &metrics_collector;
		start_context.stage_budget_last_log_ns = &stage_budget_last_log_ns;
		start_context.processed_frames = &processed_frames;
		start_context.submit_similarity_consecutive_skips =
			&submit_similarity_consecutive_skips;
		start_context.submit_similarity_prev_sample = &submit_similarity_prev_sample;
		start_context.submit_similarity_scratch_sample = &submit_similarity_scratch_sample;
		start_context.health = &health;
		detail::ResetStateForStart(start_context);
#if defined(LENSES_ENABLE_ORT)
		coreml_requested = false;
		coreml_enabled = false;
		cpu_ep_fallback_disabled = false;
		coreml_coverage_stats = {};
#endif

		lenses::ai::tracking::ByteTrackConfig tracker_config{};
		tracker_config.track_buffer = std::max<uint32_t>(12U, config.ai_fps_target * 2U);
		tracker = std::make_unique<lenses::ai::tracking::ByteTrackTracker>(tracker_config);

#if defined(LENSES_ENABLE_ORT)
		std::string init_failure_reason;
		session_ready = InitializeSession(init_failure_reason);
#else
		std::string init_failure_reason = "ONNX Runtime support is disabled at build time";
		session_ready = false;
#endif

		if (!session_ready) {
			std::scoped_lock lock(mutex);
			health.ready = false;
			health.fallback_active = false;
			health.backend = "ort";
			health.coreml_requested = coreml_requested;
			health.coreml_enabled = coreml_enabled;
			health.cpu_ep_fallback_disabled = cpu_ep_fallback_disabled;
			health.detail = init_failure_reason.empty()
						? "ORT initialization failed"
						: init_failure_reason;
			blog(LOG_ERROR,
			     "[lenses] ORT initialization failed. runtime not started. model='%s' requested_provider='%s' reason='%s'",
			     config.model_path.c_str(), config.execution_provider.c_str(),
			     init_failure_reason.empty() ? "unknown" : init_failure_reason.c_str());
			return false;
		}

		{
			std::scoped_lock lock(mutex);
			detail::SetHealthReady(health, active_execution_provider);
			health.cpu_ep_fallback_disabled = cpu_ep_fallback_disabled;
		}

		bool strict_cpu_fallback_blocked = false;
		std::string strict_cpu_fallback_reason;
		{
			std::scoped_lock lock(mutex);
			if (config.strict_runtime_checks && health.cpu_ep_fallback_detected) {
				health.ready = false;
				health.fallback_active = false;
				health.backend = "ort";
				health.detail =
					"Strict runtime checks rejected session: CPU fallback detected "
					"(provider=" +
					active_execution_provider + ", supported_nodes=" +
					std::to_string(health.coreml_supported_nodes) + "/" +
					std::to_string(health.coreml_total_nodes) +
					", partitions=" +
					std::to_string(health.coreml_supported_partitions) + ")";
				strict_cpu_fallback_reason = health.detail;
				strict_cpu_fallback_blocked = true;
			} else if (config.strict_runtime_checks && coreml_requested &&
				   coreml_enabled && !cpu_ep_fallback_disabled &&
				   !health.coreml_coverage_known) {
				health.ready = false;
				health.fallback_active = false;
				health.backend = "ort";
				health.detail =
					"Strict runtime checks rejected session: unable to verify "
					"CPU fallback state (disable_cpu_ep_fallback unsupported "
					"and CoreML coverage unknown)";
				strict_cpu_fallback_reason = health.detail;
				strict_cpu_fallback_blocked = true;
			}
		}
		if (strict_cpu_fallback_blocked) {
			blog(LOG_ERROR,
			     "[lenses] ORT strict runtime gate blocked session start: %s",
			     strict_cpu_fallback_reason.c_str());
			Stop();
			{
				std::scoped_lock lock(mutex);
				health.ready = false;
				health.fallback_active = false;
				health.backend = "ort";
				health.detail = strict_cpu_fallback_reason;
			}
			return false;
		}

#if defined(LENSES_ENABLE_ORT)
		blog(LOG_INFO,
		     "[lenses] ORT session ready. model='%s' provider='%s' input=%ux%u proto=%ux%u mask_dim=%u",
		     config.model_path.c_str(), config.execution_provider.c_str(), input_width, input_height,
		     proto_width, proto_height, mask_dim);
#else
		blog(LOG_INFO, "[lenses] ORT runtime started. model='%s' provider='%s'",
		     config.model_path.c_str(), config.execution_provider.c_str());
#endif

		running = true;
		stop_requested = false;
		worker = std::thread(&Impl::WorkerLoop, this);
		return true;
	}

	void Stop()
	{
		{
			std::scoped_lock lock(mutex);
			detail::MarkStopping(stop_requested, running);
		}
		cv.notify_all();
		if (worker.joinable())
			worker.join();

		std::scoped_lock lock(mutex);
		detail::LifecycleStopContext stop_context{};
		stop_context.submit_queue = &submit_queue;
		stop_context.output_queue = &output_queue;
		stop_context.metrics_collector = &metrics_collector;
		stop_context.stage_budget_last_log_ns = &stage_budget_last_log_ns;
		stop_context.tracker = &tracker;
		detail::ResetQueuesAndHistories(stop_context);
		detail::ResetHealthForStop(health);
#if defined(LENSES_ENABLE_ORT)
		detail::LifecycleOrtStopContext ort_stop_context{};
		ort_stop_context.session = &session;
		ort_stop_context.env = &env;
		ort_stop_context.session_ready = &session_ready;
		ort_stop_context.io_binding = &io_binding;
		ort_stop_context.io_binding_enabled = &io_binding_enabled;
		ort_stop_context.io_binding_static_outputs = &io_binding_static_outputs;
		ort_stop_context.io_binding_dynamic_outputs = &io_binding_dynamic_outputs;
		ort_stop_context.io_binding_output_memory_info = &io_binding_output_memory_info;
		ort_stop_context.bound_detection_shape = &bound_detection_shape;
		ort_stop_context.bound_proto_shape = &bound_proto_shape;
		ort_stop_context.bound_detection_storage = &bound_detection_storage;
		ort_stop_context.bound_proto_storage = &bound_proto_storage;
		ort_stop_context.bound_detection_output = &bound_detection_output;
		ort_stop_context.bound_proto_output = &bound_proto_output;
		ort_stop_context.active_execution_provider = &active_execution_provider;
		ort_stop_context.coreml_requested = &coreml_requested;
		ort_stop_context.coreml_enabled = &coreml_enabled;
		detail::ResetOrtSessionState(ort_stop_context);
		cpu_ep_fallback_disabled = false;
		coreml_coverage_stats = {};
#endif
	}

	bool SubmitFrame(lenses::core::FrameTicket frame)
	{
		std::scoped_lock lock(mutex);
		if (!running)
			return false;

		const uint64_t now_ns = os_gettime_ns();
		detail::SubmitRuntimeState submit_state{};
		submit_state.config = &config;
		submit_state.submit_queue = &submit_queue;
		submit_state.stats = &stats;
		submit_state.submit_similarity_prev_sample = &submit_similarity_prev_sample;
		submit_state.submit_similarity_scratch_sample = &submit_similarity_scratch_sample;
		submit_state.submit_similarity_consecutive_skips =
			&submit_similarity_consecutive_skips;
		submit_state.metrics_collector = &metrics_collector;
		if (!detail::HandleSubmitQueueCapacity(submit_state, now_ns))
			return false;

		if (detail::ShouldSkipBySimilarity(submit_state, frame))
			return true;
		submit_similarity_consecutive_skips = 0;

		detail::EnqueueSubmitFrame(submit_state, std::move(frame), now_ns);
		cv.notify_one();
		return true;
	}

	std::optional<lenses::core::MaskFrame> TryPopMaskFrame()
	{
		std::scoped_lock lock(mutex);
		if (!output_queue.empty()) {
			lenses::core::MaskFrame frame = std::move(output_queue.front());
			output_queue.pop_front();
			stats.output_queue_depth = output_queue.size();
			return frame;
		}

		return std::nullopt;
	}

	[[nodiscard]] lenses::core::MaskGeneratorStats GetStats() const
	{
		std::scoped_lock lock(mutex);
		lenses::core::MaskGeneratorStats current = stats;
		const uint64_t now_ns = os_gettime_ns();
		metrics_collector.Snapshot(current, now_ns, submit_queue.size(), output_queue.size());
		return current;
	}

	[[nodiscard]] lenses::core::MaskGeneratorHealth GetHealth() const
	{
		std::scoped_lock lock(mutex);
		return health;
	}

	private:
#if defined(LENSES_ENABLE_ORT)
	enum class InputLayout {
		NCHW,
		NHWC,
	};

	using StageMetrics = detail::WorkerStageMetrics;

	struct CoreMLCoverageStats {
		bool known = false;
		uint32_t supported_partitions = 0;
		uint32_t total_nodes = 0;
		uint32_t supported_nodes = 0;
	};

	static void ORT_API_CALL OnnxRuntimeLogCallback(void *logger_param, OrtLoggingLevel severity,
							const char *category, const char *logid,
							const char *code_location, const char *message)
	{
		UNUSED_PARAMETER(severity);
		UNUSED_PARAMETER(category);
		UNUSED_PARAMETER(logid);
		UNUSED_PARAMETER(code_location);
		auto *self = static_cast<Impl *>(logger_param);
		if (!self || !message)
			return;

		uint32_t partitions = 0;
		uint32_t total_nodes = 0;
		uint32_t supported_nodes = 0;
		if (!detail::TryParseCoreMLCapabilityLog(message, partitions, total_nodes, supported_nodes))
			return;

		std::scoped_lock lock(self->mutex);
		self->coreml_coverage_stats.known = true;
		self->coreml_coverage_stats.supported_partitions = partitions;
		self->coreml_coverage_stats.total_nodes = total_nodes;
		self->coreml_coverage_stats.supported_nodes = supported_nodes;
	}

	bool InitializeSession(std::string &error_out)
	{
		input_layout = InputLayout::NCHW;
		detail::SessionOrchestrationContext session_context{};
		session_context.config = &config;
		session_context.env = &env;
		session_context.session = &session;
		session_context.log_callback = &Impl::OnnxRuntimeLogCallback;
		session_context.log_callback_user_data = this;
		session_context.active_execution_provider = &active_execution_provider;
		session_context.coreml_requested = &coreml_requested;
		session_context.coreml_enabled = &coreml_enabled;
		session_context.cpu_ep_fallback_disabled = &cpu_ep_fallback_disabled;
		session_context.session_runtime_io_state.input_name_storage = &input_name_storage;
		session_context.session_runtime_io_state.output_name_storage = &output_name_storage;
		session_context.session_runtime_io_state.selected_input_name = &selected_input_name;
		session_context.session_runtime_io_state.detection_output_index =
			&detection_output_index;
		session_context.session_runtime_io_state.proto_output_index = &proto_output_index;
		session_context.session_runtime_io_state.input_names = &input_names;
		session_context.session_runtime_io_state.output_names = &output_names;
		session_context.session_runtime_io_state.input_width = &input_width;
		session_context.session_runtime_io_state.input_height = &input_height;
		session_context.session_runtime_state.mask_dim = &mask_dim;
		session_context.session_runtime_state.proto_width = &proto_width;
		session_context.session_runtime_state.proto_height = &proto_height;
		session_context.session_runtime_state.proto_channel_first = &proto_channel_first;
		session_context.session_runtime_state.io_binding_enabled = &io_binding_enabled;
		session_context.session_runtime_state.io_binding.session = session.get();
		session_context.session_runtime_state.io_binding.output_name_storage =
			&output_name_storage;
		session_context.session_runtime_state.io_binding.io_binding = &io_binding;
		session_context.session_runtime_state.io_binding.io_binding_static_outputs =
			&io_binding_static_outputs;
		session_context.session_runtime_state.io_binding.io_binding_dynamic_outputs =
			&io_binding_dynamic_outputs;
		session_context.session_runtime_state.io_binding.io_binding_output_memory_info =
			&io_binding_output_memory_info;
		session_context.session_runtime_state.io_binding.bound_detection_shape =
			&bound_detection_shape;
		session_context.session_runtime_state.io_binding.bound_proto_shape = &bound_proto_shape;
		session_context.session_runtime_state.io_binding.bound_detection_storage =
			&bound_detection_storage;
		session_context.session_runtime_state.io_binding.bound_proto_storage =
			&bound_proto_storage;
		session_context.session_runtime_state.io_binding.bound_detection_output =
			&bound_detection_output;
		session_context.session_runtime_state.io_binding.bound_proto_output =
			&bound_proto_output;
		session_context.io_binding_runtime_state =
			session_context.session_runtime_state.io_binding;
		session_context.max_input_dimension = kMaxInputDimension;

		{
			std::scoped_lock lock(mutex);
			coreml_coverage_stats = {};
		}
		if (!detail::InitializeOrtSessionRuntime(session_context, error_out))
			return false;
		RefreshCoreMLHealthSnapshot();
		error_out.clear();
		return true;
	}

	void RefreshCoreMLHealthSnapshot()
	{
		std::scoped_lock lock(mutex);
		detail::UpdateCoreMLHealthSnapshot(
			health, coreml_requested, coreml_enabled, coreml_coverage_stats.known,
			coreml_coverage_stats.supported_partitions,
			coreml_coverage_stats.supported_nodes,
			coreml_coverage_stats.total_nodes);
		health.cpu_ep_fallback_disabled = cpu_ep_fallback_disabled;
	}

	bool RunInference(const lenses::core::FrameTicket &frame, lenses::core::MaskFrame &out_frame,
			  StageMetrics &metrics)
		{
			if (!session || frame.image_bgra.empty() || frame.image_width == 0 || frame.image_height == 0)
				return false;

			metrics.readback_ms = frame.readback_ms;

			const bool nchw = input_layout == InputLayout::NCHW;
			if (!detail::PrepareOrtInputTensorValues(
				    frame, config, input_width, input_height, nchw,
				    input_tensor_values_cache, resized_bgra_cache, plane_r_cache,
				    plane_g_cache, plane_b_cache, plane_a_cache, plane_r_float_cache,
				    plane_g_float_cache, plane_b_float_cache, metrics.preprocess_ms))
				return false;

			const auto input_shape =
				detail::BuildOrtInputShape(nchw, input_width, input_height);
			Ort::Value input_tensor =
				detail::CreateCpuInputTensor(input_tensor_values_cache, input_shape);

			const auto infer_started = std::chrono::steady_clock::now();
			detail::OrtInferenceOutputs ort_outputs{};
			if (!detail::ExecuteOrtInference(
				    *session, selected_input_name, output_names,
				    detection_output_index, proto_output_index, output_name_storage,
				    io_binding_enabled && config.enable_iobinding, io_binding_static_outputs,
				    io_binding_dynamic_outputs, io_binding.get(), bound_detection_storage,
				    bound_proto_storage, bound_detection_shape, bound_proto_shape,
				    input_tensor, ort_outputs))
				return false;
			metrics.infer_ms = std::chrono::duration<double, std::milli>(
						       std::chrono::steady_clock::now() - infer_started)
						       .count();

			detail::DecodeLimits decode_limits{};
			decode_limits.max_detection_features = kMaxDetectionFeatures;
			decode_limits.max_detection_candidates = kMaxDetectionCandidates;
			decode_limits.max_proto_dimension = kMaxProtoDimension;
			decode_limits.default_coco_class_count = kDefaultCocoClassCount;
			decode_limits.max_detections = kMaxDetections;
			decode_limits.confidence_threshold = kDefaultConfidenceThreshold;
			decode_limits.nms_iou_threshold = kNmsIouThreshold;
			decode_limits.mask_threshold = kDefaultMaskThreshold;
			if (!detail::DecodeOrtOutputsToMaskFrame(
				    ort_outputs, frame, input_width, input_height, decode_limits, mask_dim,
				    proto_width, proto_height, proto_channel_first, out_frame,
				    metrics.decode_ms))
				return false;
			return true;
		}
#endif

		bool ExecuteInferenceAndTracking(const lenses::core::FrameTicket &frame,
						 lenses::core::MaskFrame &mask_frame,
						 StageMetrics &stage_metrics,
						 bool &failed_with_exception,
						 const char *&failure_reason)
		{
			bool ok = false;
#if defined(LENSES_ENABLE_ORT)
			try {
				ok = RunInference(frame, mask_frame, stage_metrics);
			} catch (const Ort::Exception &ex) {
				ok = false;
				failed_with_exception = true;
				failure_reason = ex.what();
			} catch (const std::exception &ex) {
				ok = false;
				failed_with_exception = true;
				failure_reason = ex.what();
			} catch (...) {
				ok = false;
				failed_with_exception = true;
				failure_reason = "unknown exception";
			}
#endif
			if (ok && tracker) {
				const auto track_started = std::chrono::steady_clock::now();
				try {
					tracker->Update(mask_frame);
					stage_metrics.track_ms = std::chrono::duration<double, std::milli>(
									 std::chrono::steady_clock::now() - track_started)
									.count();
				} catch (const std::exception &ex) {
					ok = false;
					failed_with_exception = true;
					failure_reason = ex.what();
				} catch (...) {
					ok = false;
					failed_with_exception = true;
					failure_reason = "unknown tracker exception";
				}
			}
			return ok;
		}

		void WorkerLoop()
		{
			detail::WorkerLoopContext worker_context{};
			worker_context.config = &config;
			worker_context.mutex = &mutex;
			worker_context.cv = &cv;
			worker_context.submit_queue = &submit_queue;
			worker_context.output_queue = &output_queue;
			worker_context.stats = &stats;
			worker_context.health = &health;
			worker_context.metrics_collector = &metrics_collector;
			worker_context.stop_requested = &stop_requested;
			worker_context.processed_frames = &processed_frames;
			worker_context.stage_budget_last_log_ns = &stage_budget_last_log_ns;
			worker_context.worker_state.config = &config;
			worker_context.worker_state.output_queue = &output_queue;
			worker_context.worker_state.stats = &stats;
			worker_context.worker_state.mutex = &mutex;
			worker_context.worker_state.input_width = &input_width;
			worker_context.worker_state.input_height = &input_height;
			worker_context.worker_state.proto_width = &proto_width;
			worker_context.worker_state.proto_height = &proto_height;
			worker_context.worker_state.mask_dim = &mask_dim;
			worker_context.worker_state.detection_output_index = &detection_output_index;
			worker_context.worker_state.proto_output_index = &proto_output_index;
			worker_context.worker_state.metrics_collector = &metrics_collector;
			worker_context.run_inference =
				[this](const lenses::core::FrameTicket &frame,
				       lenses::core::MaskFrame &mask_frame,
				       StageMetrics &stage_metrics,
				       bool &failed_with_exception,
				       const char *&failure_reason) {
					return ExecuteInferenceAndTracking(
						frame, mask_frame, stage_metrics,
						failed_with_exception, failure_reason);
				};
			detail::RunWorkerLoop(worker_context);
		}
	lenses::core::RuntimeConfig config{};

	mutable std::mutex mutex;
	std::condition_variable cv;
	std::deque<lenses::core::FrameTicket> submit_queue;
	std::deque<lenses::core::MaskFrame> output_queue;
	lenses::core::MaskGeneratorStats stats{};
	std::thread worker;
	bool running = false;
	bool stop_requested = false;
	bool session_ready = false;
	uint64_t processed_frames = 0;
	uint32_t submit_similarity_consecutive_skips = 0;
	uint64_t stage_budget_last_log_ns = 0;
	std::vector<uint8_t> submit_similarity_prev_sample;
	std::vector<uint8_t> submit_similarity_scratch_sample;
	lenses::core::MaskGeneratorHealth health{};
	detail::RuntimeMetricsCollector metrics_collector{};

	std::unique_ptr<lenses::ai::tracking::ByteTrackTracker> tracker;

#if defined(LENSES_ENABLE_ORT)
	std::unique_ptr<Ort::Env> env;
	std::unique_ptr<Ort::Session> session;
	InputLayout input_layout = InputLayout::NCHW;
	uint32_t input_width = 0;
	uint32_t input_height = 0;
	uint32_t mask_dim = 0;
	uint32_t proto_width = 0;
	uint32_t proto_height = 0;
	bool proto_channel_first = true;
	size_t detection_output_index = 0;
	size_t proto_output_index = 1;
	std::vector<std::string> input_name_storage;
	std::vector<std::string> output_name_storage;
	std::vector<const char *> input_names;
	std::vector<const char *> output_names;
	std::string selected_input_name;
	std::vector<float> input_tensor_values_cache;
	std::vector<uint8_t> resized_bgra_cache;
	std::vector<uint8_t> plane_r_cache;
	std::vector<uint8_t> plane_g_cache;
	std::vector<uint8_t> plane_b_cache;
	std::vector<uint8_t> plane_a_cache;
	std::vector<float> plane_r_float_cache;
	std::vector<float> plane_g_float_cache;
	std::vector<float> plane_b_float_cache;
	std::string active_execution_provider = "cpu";
	bool io_binding_enabled = false;
	bool coreml_requested = false;
	bool coreml_enabled = false;
	bool cpu_ep_fallback_disabled = false;
	CoreMLCoverageStats coreml_coverage_stats{};
	bool io_binding_static_outputs = false;
	bool io_binding_dynamic_outputs = false;
	std::unique_ptr<Ort::IoBinding> io_binding;
	std::unique_ptr<Ort::MemoryInfo> io_binding_output_memory_info;
	std::vector<int64_t> bound_detection_shape;
	std::vector<int64_t> bound_proto_shape;
	std::vector<float> bound_detection_storage;
	std::vector<float> bound_proto_storage;
	std::unique_ptr<Ort::Value> bound_detection_output;
	std::unique_ptr<Ort::Value> bound_proto_output;
#endif
	};

OnnxMaskGenerator::OnnxMaskGenerator() : impl_(std::make_unique<Impl>()) {}

OnnxMaskGenerator::~OnnxMaskGenerator() = default;

bool OnnxMaskGenerator::Start(const lenses::core::RuntimeConfig &config)
{
	return impl_->Start(config);
}

void OnnxMaskGenerator::Stop()
{
	impl_->Stop();
}

bool OnnxMaskGenerator::SubmitFrame(lenses::core::FrameTicket frame)
{
	return impl_->SubmitFrame(std::move(frame));
}

std::optional<lenses::core::MaskFrame> OnnxMaskGenerator::TryPopMaskFrame()
{
	return impl_->TryPopMaskFrame();
}

lenses::core::MaskGeneratorStats OnnxMaskGenerator::GetStats() const
{
	return impl_->GetStats();
}

lenses::core::MaskGeneratorHealth OnnxMaskGenerator::GetHealth() const
{
	return impl_->GetHealth();
}

} // namespace lenses::ai::ort
