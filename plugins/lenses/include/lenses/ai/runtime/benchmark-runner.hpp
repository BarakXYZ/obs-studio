#pragma once

#include "lenses/core/interfaces.hpp"

#include <cstdint>
#include <string>

namespace lenses::ai::runtime {

struct BenchmarkRunnerConfig {
	lenses::core::RuntimeConfig runtime_config{};
	std::string clip_path;
	uint32_t source_width = 1920;
	uint32_t source_height = 1080;
	uint32_t image_width = 0;
	uint32_t image_height = 0;
	uint32_t frame_count = 240;
	uint32_t warmup_frame_count = 0;
	uint32_t submit_fps = 30;
	uint32_t drain_timeout_ms = 8000;
	uint64_t timestamp_start_ns = 0;
	bool wall_clock_pacing = false;
};

struct BenchmarkReport {
	bool success = false;
	std::string error;
	uint64_t submitted_frames = 0;
	uint64_t rejected_frames = 0;
	uint64_t completed_frames = 0;
	double wall_time_ms = 0.0;
	double effective_submit_fps = 0.0;
	double effective_complete_fps = 0.0;
	lenses::core::MaskGeneratorStats stats{};
	lenses::core::MaskGeneratorHealth health{};
};

BenchmarkReport RunBenchmark(const BenchmarkRunnerConfig &config);

} // namespace lenses::ai::runtime
