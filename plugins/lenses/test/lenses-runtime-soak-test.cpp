#include "lenses/core/noop-mask-generator.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

bool WaitForProgress(lenses::core::NoopMaskGenerator &generator, uint64_t min_completed,
		     std::chrono::milliseconds timeout)
{
	const auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < timeout) {
		auto stats = generator.GetStats();
		if (stats.completed_frames >= min_completed)
			return true;
		(void)generator.TryPopMaskFrame();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

} // namespace

int main()
{
	lenses::core::NoopMaskGenerator generator;
	lenses::core::RuntimeConfig config{};
	config.ai_fps_target = 1000;
	config.submit_queue_limit = 8;
	config.output_queue_limit = 4;
	config.fallback_to_last_mask = true;

	if (!generator.Start(config)) {
		std::cerr << "FAIL: failed to start generator" << std::endl;
		return 1;
	}

	constexpr uint64_t kFrames = 1500;
	for (uint64_t i = 0; i < kFrames; ++i) {
		lenses::core::FrameTicket ticket{};
		ticket.frame_id = i + 1;
		ticket.source_width = 1920;
		ticket.source_height = 1080;
		ticket.timestamp_ns = (i + 1) * 1000;
		if (!generator.SubmitFrame(ticket)) {
			std::cerr << "FAIL: submit rejected at frame " << i << std::endl;
			generator.Stop();
			return 1;
		}
		if ((i % 32) == 0)
			(void)generator.TryPopMaskFrame();
		if ((i % 4) == 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	const bool progressed = WaitForProgress(generator, 20, std::chrono::seconds(8));
	const auto stats = generator.GetStats();
	generator.Stop();

	if (!progressed) {
		std::cerr << "FAIL: generator did not make expected progress" << std::endl;
		return 1;
	}
	if (stats.submitted_frames < kFrames) {
		std::cerr << "FAIL: submitted frame count regressed" << std::endl;
		return 1;
	}
	if (stats.completed_frames == 0) {
		std::cerr << "FAIL: no completed frames were produced" << std::endl;
		return 1;
	}
	if (stats.submit_queue_depth > config.submit_queue_limit) {
		std::cerr << "FAIL: submit queue depth exceeded configured bound" << std::endl;
		return 1;
	}

	std::cout << "lenses-runtime-soak-test: PASS\n"
		  << "submitted=" << stats.submitted_frames << " completed=" << stats.completed_frames
		  << " dropped=" << stats.dropped_frames << " reused=" << stats.reused_last_mask_frames
		  << " submit_q=" << stats.submit_queue_depth << " output_q=" << stats.output_queue_depth
		  << std::endl;
	return 0;
}
