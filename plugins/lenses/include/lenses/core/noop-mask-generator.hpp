#pragma once

#include "lenses/core/interfaces.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace lenses::core {

class NoopMaskGenerator final : public IMaskGenerator {
public:
	~NoopMaskGenerator() override;

	bool Start(const RuntimeConfig &config) override;
	void Stop() override;
	bool SubmitFrame(FrameTicket frame) override;
	std::optional<MaskFrame> TryPopMaskFrame() override;
	[[nodiscard]] MaskGeneratorStats GetStats() const override;
	[[nodiscard]] MaskGeneratorHealth GetHealth() const override;

private:
	void WorkerLoop();

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	RuntimeConfig config_{};
	bool running_ = false;
	bool stop_requested_ = false;
	std::thread worker_;
	MaskGeneratorStats stats_{};
	std::deque<FrameTicket> submit_queue_{};
	std::deque<MaskFrame> output_queue_{};
	std::unique_ptr<IInstanceTracker> tracker_{};
};

} // namespace lenses::core
