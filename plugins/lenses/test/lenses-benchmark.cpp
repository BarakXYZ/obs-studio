#include "lenses/ai/runtime/benchmark-runner.hpp"

#include <obs-data.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

const char *FindArgValue(int argc, char **argv, const char *flag)
{
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::string(argv[i]) == flag)
			return argv[i + 1];
	}
	return nullptr;
}

bool HasFlag(int argc, char **argv, const char *flag)
{
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == flag)
			return true;
	}
	return false;
}

uint32_t ParseUintOrDefault(const char *value, uint32_t fallback)
{
	if (!value || !*value)
		return fallback;
	char *end_ptr = nullptr;
	const unsigned long parsed = std::strtoul(value, &end_ptr, 10);
	if (!end_ptr || *end_ptr != '\0')
		return fallback;
	if (parsed == 0 || parsed > 1000000UL)
		return fallback;
	return (uint32_t)parsed;
}

uint32_t ParseUintAllowZeroOrDefault(const char *value, uint32_t fallback)
{
	if (!value || !*value)
		return fallback;
	char *end_ptr = nullptr;
	const unsigned long parsed = std::strtoul(value, &end_ptr, 10);
	if (!end_ptr || *end_ptr != '\0')
		return fallback;
	if (parsed > 1000000UL)
		return fallback;
	return (uint32_t)parsed;
}

struct ModelRuntimeTraits {
	bool loaded = false;
	bool dynamic_shape = true;
	bool static_input = false;
	bool static_output = false;
	bool supports_iobinding_static_outputs = false;
};

bool BuildSiblingPath(const std::string &file_path, const char *sibling_name, std::string &out_path)
{
	if (!sibling_name || !*sibling_name)
		return false;
	const size_t slash = file_path.find_last_of('/');
	if (slash == std::string::npos)
		return false;
	out_path.assign(file_path.substr(0, slash + 1U));
	out_path += sibling_name;
	return true;
}

ModelRuntimeTraits LoadModelRuntimeTraits(const char *model_path)
{
	ModelRuntimeTraits traits{};
	if (!model_path || !*model_path)
		return traits;

	std::string metadata_path;
	if (!BuildSiblingPath(model_path, "metadata.json", metadata_path))
		return traits;

	obs_data_t *root = obs_data_create_from_json_file(metadata_path.c_str());
	if (!root)
		return traits;

	obs_data_t *model = obs_data_get_obj(root, "model");
	if (!model) {
		obs_data_release(root);
		return traits;
	}

	if (obs_data_has_user_value(model, "dynamic"))
		traits.dynamic_shape = obs_data_get_bool(model, "dynamic");
	if (obs_data_has_user_value(model, "static_input"))
		traits.static_input = obs_data_get_bool(model, "static_input");
	else
		traits.static_input = !traits.dynamic_shape;
	if (obs_data_has_user_value(model, "static_output"))
		traits.static_output = obs_data_get_bool(model, "static_output");
	if (obs_data_has_user_value(model, "supports_iobinding_static_outputs"))
		traits.supports_iobinding_static_outputs =
			obs_data_get_bool(model, "supports_iobinding_static_outputs");
	else
		traits.supports_iobinding_static_outputs = traits.static_output;

	traits.loaded = true;
	obs_data_release(model);
	obs_data_release(root);
	return traits;
}

void PrintUsage()
{
	std::cout << "Usage: lenses-benchmark --model <path> [--provider ort-coreml]"
		     << " [--clip /abs/path/to/clip.mp4]"
		     << " [--input-width 640] [--input-height 640] [--source-width 1920]"
		     << " [--source-height 1080] [--frames 240] [--submit-fps 30]"
		     << " [--target-fps 30] [--warmup-frames 60] [--drain-timeout-ms 8000] [--wall-clock]"
		     << " [--allow-fallback] [--force-static-input-shape]" << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
	const char *model_path = FindArgValue(argc, argv, "--model");
	if (!model_path || !*model_path) {
		PrintUsage();
		return 2;
	}

	const char *provider_arg = FindArgValue(argc, argv, "--provider");
	const std::string provider = provider_arg && *provider_arg ? provider_arg : "ort-coreml";
	const char *clip_arg = FindArgValue(argc, argv, "--clip");

	const uint32_t input_width = ParseUintOrDefault(FindArgValue(argc, argv, "--input-width"), 640);
	const uint32_t input_height = ParseUintOrDefault(FindArgValue(argc, argv, "--input-height"), 640);
	const uint32_t source_width = ParseUintOrDefault(FindArgValue(argc, argv, "--source-width"), 1920);
	const uint32_t source_height = ParseUintOrDefault(FindArgValue(argc, argv, "--source-height"), 1080);
	const uint32_t frame_count = ParseUintOrDefault(FindArgValue(argc, argv, "--frames"), 240);
	const uint32_t submit_fps = ParseUintOrDefault(FindArgValue(argc, argv, "--submit-fps"), 30);
	const uint32_t target_fps = ParseUintOrDefault(FindArgValue(argc, argv, "--target-fps"), submit_fps);
	const uint32_t warmup_frames =
		ParseUintAllowZeroOrDefault(FindArgValue(argc, argv, "--warmup-frames"), 0);
	const uint32_t drain_timeout_ms =
		ParseUintOrDefault(FindArgValue(argc, argv, "--drain-timeout-ms"), 8000);
	const bool force_static_input_shape = HasFlag(argc, argv, "--force-static-input-shape");
	const ModelRuntimeTraits model_traits = LoadModelRuntimeTraits(model_path);

	lenses::core::RuntimeConfig runtime_config{};
	runtime_config.ai_fps_target = target_fps;
	runtime_config.input_width = input_width;
	runtime_config.input_height = input_height;
	runtime_config.inference_every_n_frames = 1;
	runtime_config.enable_similarity_skip = false;
	runtime_config.enable_iobinding = true;
	runtime_config.provider = provider;
	runtime_config.execution_provider = provider;
	runtime_config.model_path = model_path;
	runtime_config.model_dynamic_shape =
		model_traits.loaded ? model_traits.dynamic_shape : true;
	runtime_config.model_static_input =
		model_traits.loaded ? model_traits.static_input : false;
	runtime_config.model_static_output =
		model_traits.loaded ? model_traits.static_output : false;
	runtime_config.model_supports_iobinding_static_outputs =
		model_traits.loaded ? model_traits.supports_iobinding_static_outputs : false;
	if (force_static_input_shape) {
		runtime_config.model_dynamic_shape = false;
		runtime_config.model_static_input = true;
	}
	runtime_config.strict_runtime_checks = !HasFlag(argc, argv, "--allow-fallback");
	runtime_config.submit_queue_limit = 8;
	runtime_config.output_queue_limit = 4;
	runtime_config.scheduler_mode = lenses::core::SchedulerMode::Adaptive;
	runtime_config.preprocess_mode = lenses::core::PreprocessMode::Auto;
	runtime_config.drop_policy = lenses::core::DropPolicy::DropOldest;
	runtime_config.profiling_enabled = true;

	lenses::ai::runtime::BenchmarkRunnerConfig benchmark_config{};
	benchmark_config.runtime_config = runtime_config;
	benchmark_config.clip_path = clip_arg && *clip_arg ? clip_arg : "";
	benchmark_config.source_width = source_width;
	benchmark_config.source_height = source_height;
	benchmark_config.image_width = input_width;
	benchmark_config.image_height = input_height;
	benchmark_config.frame_count = frame_count;
	benchmark_config.warmup_frame_count = warmup_frames;
	benchmark_config.submit_fps = submit_fps;
	benchmark_config.drain_timeout_ms = drain_timeout_ms;
	benchmark_config.timestamp_start_ns = 1000000000ULL;
	benchmark_config.wall_clock_pacing = HasFlag(argc, argv, "--wall-clock");

	const auto report = lenses::ai::runtime::RunBenchmark(benchmark_config);
	std::cout << "lenses-benchmark\n";
	std::cout << "model_traits loaded=" << (model_traits.loaded ? 1 : 0)
		  << " dynamic=" << (runtime_config.model_dynamic_shape ? 1 : 0)
		  << " static_input=" << (runtime_config.model_static_input ? 1 : 0)
		  << " static_output=" << (runtime_config.model_static_output ? 1 : 0)
		  << " static_iobinding=" << (runtime_config.model_supports_iobinding_static_outputs ? 1 : 0)
		  << "\n";
	std::cout << "success=" << (report.success ? 1 : 0) << " error='" << report.error << "'\n";
	std::cout << "submitted=" << report.submitted_frames << " rejected=" << report.rejected_frames
		  << " completed=" << report.completed_frames << "\n";
	std::cout << "wall_ms=" << report.wall_time_ms << " submit_fps=" << report.effective_submit_fps
		  << " complete_fps=" << report.effective_complete_fps << "\n";
	std::cout << "stage_p50 readback=" << report.stats.readback_ms_p50
		  << " preprocess=" << report.stats.preprocess_ms_p50
		  << " infer=" << report.stats.infer_ms_p50
		  << " decode=" << report.stats.decode_ms_p50
		  << " track=" << report.stats.track_ms_p50
		  << " queue=" << report.stats.queue_latency_ms_p50
		  << " e2e=" << report.stats.end_to_end_latency_ms_p50 << "\n";
	std::cout << "stage_p95 readback=" << report.stats.readback_ms_p95
		  << " preprocess=" << report.stats.preprocess_ms_p95
		  << " infer=" << report.stats.infer_ms_p95
		  << " decode=" << report.stats.decode_ms_p95
		  << " track=" << report.stats.track_ms_p95
		  << " queue=" << report.stats.queue_latency_ms_p95
		  << " e2e=" << report.stats.end_to_end_latency_ms_p95 << "\n";
	std::cout << "stage_p99 readback=" << report.stats.readback_ms_p99
		  << " preprocess=" << report.stats.preprocess_ms_p99
		  << " infer=" << report.stats.infer_ms_p99
		  << " decode=" << report.stats.decode_ms_p99
		  << " track=" << report.stats.track_ms_p99
		  << " queue=" << report.stats.queue_latency_ms_p99
		  << " e2e=" << report.stats.end_to_end_latency_ms_p99 << "\n";
	std::cout << "health ready=" << (report.health.ready ? 1 : 0)
		  << " backend='" << report.health.backend << "'"
		  << " requested_provider='" << runtime_config.execution_provider << "'"
		  << " coreml_requested=" << (report.health.coreml_requested ? 1 : 0)
		  << " coreml_enabled=" << (report.health.coreml_enabled ? 1 : 0)
		  << " coreml_coverage_known=" << (report.health.coreml_coverage_known ? 1 : 0)
		  << " coreml_supported_nodes=" << report.health.coreml_supported_nodes
		  << " coreml_total_nodes=" << report.health.coreml_total_nodes
		  << " coreml_supported_partitions=" << report.health.coreml_supported_partitions
		  << " coreml_coverage_ratio=" << report.health.coreml_coverage_ratio
		  << " cpu_fallback_detected=" << (report.health.cpu_ep_fallback_detected ? 1 : 0)
		  << " cpu_fallback_disabled=" << (report.health.cpu_ep_fallback_disabled ? 1 : 0)
		  << " detail='" << report.health.detail
		  << "'\n";

	return report.success ? 0 : 1;
}
