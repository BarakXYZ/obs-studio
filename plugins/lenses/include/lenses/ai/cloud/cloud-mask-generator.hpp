#pragma once

#include "lenses/core/interfaces.hpp"

#include <memory>
#include <mutex>

namespace lenses::ai::cloud {

class CloudMaskGenerator final : public lenses::core::IMaskGenerator {
public:
	explicit CloudMaskGenerator(std::unique_ptr<lenses::core::IMaskGenerator> local_fallback);
	~CloudMaskGenerator() override;

	bool Start(const lenses::core::RuntimeConfig &config) override;
	void Stop() override;
	bool SubmitFrame(lenses::core::FrameTicket frame) override;
	std::optional<lenses::core::MaskFrame> TryPopMaskFrame() override;
	[[nodiscard]] lenses::core::MaskGeneratorStats GetStats() const override;
	[[nodiscard]] lenses::core::MaskGeneratorHealth GetHealth() const override;

private:
	mutable std::mutex mutex_;
	lenses::core::RuntimeConfig config_{};
	std::unique_ptr<lenses::core::IMaskGenerator> fallback_{};
	lenses::core::MaskGeneratorStats cloud_stats_{};
	bool running_ = false;
};

} // namespace lenses::ai::cloud
