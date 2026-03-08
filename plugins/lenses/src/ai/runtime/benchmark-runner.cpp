#include "lenses/ai/runtime/benchmark-runner.hpp"

#include "lenses/ai/runtime/mask-generator-factory.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace lenses::ai::runtime {

namespace {

void FillDeterministicBgraFrame(std::vector<uint8_t> &buffer, uint32_t width, uint32_t height,
				 uint64_t frame_index)
{
	const size_t line_size = (size_t)width * 4U;
	if (buffer.size() != line_size * (size_t)height)
		buffer.resize(line_size * (size_t)height);

	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const size_t offset = (size_t)y * line_size + (size_t)x * 4U;
			const uint8_t b = (uint8_t)((x + frame_index) & 0xFFU);
			const uint8_t g = (uint8_t)((y + (frame_index * 3U)) & 0xFFU);
			const uint8_t r = (uint8_t)(((x ^ y) + (frame_index * 7U)) & 0xFFU);
			buffer[offset + 0] = b;
			buffer[offset + 1] = g;
			buffer[offset + 2] = r;
			buffer[offset + 3] = 255;
		}
	}
}

std::string QuoteForShell(const std::string &value)
{
	std::string quoted;
	quoted.reserve(value.size() + 8);
	quoted.push_back('\'');
	for (const char ch : value) {
		if (ch == '\'')
			quoted += "'\\''";
		else
			quoted.push_back(ch);
	}
	quoted.push_back('\'');
	return quoted;
}

class ClipFrameReader {
public:
	ClipFrameReader() = default;
	ClipFrameReader(const ClipFrameReader &) = delete;
	ClipFrameReader &operator=(const ClipFrameReader &) = delete;

	~ClipFrameReader() { Close(); }

	bool Open(const std::string &clip_path, uint32_t width, uint32_t height, uint32_t frame_count,
		  std::string &error_out)
	{
		Close();
		error_out.clear();
		if (clip_path.empty()) {
			error_out = "clip path is empty";
			return false;
		}
		if (width == 0 || height == 0) {
			error_out = "clip decode dimensions must be > 0";
			return false;
		}
		if (frame_count == 0) {
			error_out = "clip frame_count must be > 0";
			return false;
		}

		const std::string command = BuildCommand(clip_path, width, height, frame_count);
		pipe_ = popen(command.c_str(), "r");
		if (!pipe_) {
			error_out = std::string("failed to start ffmpeg process: ") + std::strerror(errno);
			return false;
		}

		frame_bytes_ = (size_t)width * (size_t)height * 4U;
		return true;
	}

	bool ReadFrame(std::vector<uint8_t> &buffer, std::string &error_out)
	{
		error_out.clear();
		if (!pipe_) {
			error_out = "ffmpeg clip reader is not open";
			return false;
		}
		if (frame_bytes_ == 0) {
			error_out = "invalid clip frame size";
			return false;
		}
		if (buffer.size() != frame_bytes_)
			buffer.resize(frame_bytes_);

		size_t offset = 0;
		while (offset < frame_bytes_) {
			const size_t bytes_read =
				fread(buffer.data() + offset, 1, frame_bytes_ - offset, pipe_);
			if (bytes_read > 0) {
				offset += bytes_read;
				continue;
			}
			if (ferror(pipe_)) {
				error_out = std::string("ffmpeg read error: ") + std::strerror(errno);
			} else {
				error_out = "ffmpeg stream ended before expected frame count";
			}
			return false;
		}

		return true;
	}

private:
	static std::string BuildCommand(const std::string &clip_path, uint32_t width, uint32_t height,
					uint32_t frame_count)
	{
		std::ostringstream command;
		command << "ffmpeg -hide_banner -loglevel error -nostdin -stream_loop -1 -i "
			<< QuoteForShell(clip_path) << " -an -sn"
			<< " -vf " << QuoteForShell("scale=" + std::to_string(width) + ":" +
					       std::to_string(height) +
					       ":flags=bilinear,format=bgra")
			<< " -pix_fmt bgra -vsync 0 -frames:v " << frame_count
			<< " -f rawvideo pipe:1";
		return command.str();
	}

	void Close()
	{
		if (!pipe_)
			return;
		(void)pclose(pipe_);
		pipe_ = nullptr;
		frame_bytes_ = 0;
	}

	FILE *pipe_ = nullptr;
	size_t frame_bytes_ = 0;
};

struct BenchmarkPassReport {
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

BenchmarkPassReport RunBenchmarkPass(const BenchmarkRunnerConfig &config, uint32_t frame_count,
				     uint64_t timestamp_start_ns, bool require_completion)
{
	BenchmarkPassReport pass{};
	if (frame_count == 0) {
		pass.success = true;
		return pass;
	}

	const uint32_t image_width = config.image_width > 0
					     ? config.image_width
					     : std::max<uint32_t>(1U, config.runtime_config.input_width);
	const uint32_t image_height = config.image_height > 0
					      ? config.image_height
					      : std::max<uint32_t>(1U, config.runtime_config.input_height);
	const uint64_t timestamp_step_ns =
		1000000000ULL / std::max<uint32_t>(1U, config.submit_fps);

	auto factory_result = CreateMaskGeneratorWithSelection(config.runtime_config);
	if (!factory_result.generator) {
		pass.error = factory_result.error.empty() ? "mask generator creation failed"
							 : factory_result.error;
		return pass;
	}

	auto generator = std::move(factory_result.generator);
	if (!generator->Start(factory_result.resolved_config)) {
		pass.health = generator->GetHealth();
		pass.error = pass.health.detail.empty() ? "mask generator start failed"
							 : pass.health.detail;
		generator->Stop();
		return pass;
	}

	std::vector<uint8_t> frame_bgra;
	frame_bgra.reserve((size_t)image_width * (size_t)image_height * 4U);
	ClipFrameReader clip_reader;
	if (!config.clip_path.empty()) {
		if (!clip_reader.Open(config.clip_path, image_width, image_height, frame_count,
				      pass.error)) {
			generator->Stop();
			return pass;
		}
	}

	const auto wall_start = std::chrono::steady_clock::now();
	const auto cadence_start = std::chrono::steady_clock::now();

	for (uint64_t i = 0; i < frame_count; ++i) {
		if (config.wall_clock_pacing) {
			const auto due =
				cadence_start + std::chrono::nanoseconds((long long)(i * timestamp_step_ns));
			std::this_thread::sleep_until(due);
		}

		if (!config.clip_path.empty()) {
			if (!clip_reader.ReadFrame(frame_bgra, pass.error)) {
				pass.error = "clip frame decode failed at frame " + std::to_string(i + 1U) +
					     ": " + pass.error;
				break;
			}
		} else {
			FillDeterministicBgraFrame(frame_bgra, image_width, image_height, i + 1U);
		}

		lenses::core::FrameTicket frame{};
		frame.frame_id = i + 1U;
		frame.source_width = std::max<uint32_t>(1U, config.source_width);
		frame.source_height = std::max<uint32_t>(1U, config.source_height);
		frame.timestamp_ns = timestamp_start_ns + i * timestamp_step_ns;
		frame.image_width = image_width;
		frame.image_height = image_height;
		frame.image_linesize = image_width * 4U;
		frame.readback_ms = 0.0;
		frame.image_bgra = frame_bgra;

		if (generator->SubmitFrame(std::move(frame)))
			pass.submitted_frames++;
		else
			pass.rejected_frames++;

		while (generator->TryPopMaskFrame().has_value())
			pass.completed_frames++;
	}

	const auto drain_deadline = std::chrono::steady_clock::now() +
					std::chrono::milliseconds(config.drain_timeout_ms);
	while (std::chrono::steady_clock::now() < drain_deadline) {
		auto frame = generator->TryPopMaskFrame();
		if (frame.has_value()) {
			pass.completed_frames++;
			continue;
		}
		if (pass.completed_frames >= pass.submitted_frames)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	pass.stats = generator->GetStats();
	pass.health = generator->GetHealth();
	generator->Stop();

	pass.wall_time_ms = std::chrono::duration<double, std::milli>(
				    std::chrono::steady_clock::now() - wall_start)
				    .count();
	if (pass.wall_time_ms > 0.0) {
		pass.effective_submit_fps =
			((double)pass.submitted_frames * 1000.0) / pass.wall_time_ms;
		pass.effective_complete_fps =
			((double)pass.completed_frames * 1000.0) / pass.wall_time_ms;
	}

	if (!pass.error.empty()) {
		pass.success = false;
		return pass;
	}

	if (require_completion) {
		pass.success = pass.rejected_frames == 0 &&
			      pass.completed_frames == pass.submitted_frames && pass.health.ready;
	} else {
		pass.success = pass.rejected_frames == 0 && pass.health.ready;
	}

	if (!pass.success && pass.error.empty()) {
		if (!pass.health.ready && !pass.health.detail.empty()) {
			pass.error = pass.health.detail;
		} else if (pass.rejected_frames > 0) {
			pass.error = "benchmark pass rejected submitted frames";
		} else if (require_completion) {
			pass.error = "benchmark run did not complete all submitted frames";
		} else {
			pass.error = "warmup pass did not satisfy readiness requirements";
		}
	}

	return pass;
}

} // namespace

BenchmarkReport RunBenchmark(const BenchmarkRunnerConfig &config)
{
	BenchmarkReport report{};

	if (config.frame_count == 0) {
		report.error = "frame_count must be > 0";
		return report;
	}

	const uint64_t timestamp_step_ns =
		1000000000ULL / std::max<uint32_t>(1U, config.submit_fps);
	uint64_t measured_timestamp_start_ns =
		config.timestamp_start_ns > 0 ? config.timestamp_start_ns : 1000000000ULL;

	if (config.warmup_frame_count > 0) {
		const auto warmup_pass = RunBenchmarkPass(config, config.warmup_frame_count,
							(measured_timestamp_start_ns - timestamp_step_ns),
							false);
		if (!warmup_pass.success) {
			report.error = warmup_pass.error.empty()
					 ? "warmup benchmark pass failed"
					 : ("warmup benchmark pass failed: " + warmup_pass.error);
			report.health = warmup_pass.health;
			report.stats = warmup_pass.stats;
			return report;
		}
		measured_timestamp_start_ns +=
			(uint64_t)config.warmup_frame_count * timestamp_step_ns;
	}

	const auto measured_pass =
		RunBenchmarkPass(config, config.frame_count, measured_timestamp_start_ns, true);
	report.success = measured_pass.success;
	report.error = measured_pass.error;
	report.submitted_frames = measured_pass.submitted_frames;
	report.rejected_frames = measured_pass.rejected_frames;
	report.completed_frames = measured_pass.completed_frames;
	report.wall_time_ms = measured_pass.wall_time_ms;
	report.effective_submit_fps = measured_pass.effective_submit_fps;
	report.effective_complete_fps = measured_pass.effective_complete_fps;
	report.stats = measured_pass.stats;
	report.health = measured_pass.health;
	return report;
}

} // namespace lenses::ai::runtime
