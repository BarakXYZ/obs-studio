#pragma once

#include "lenses/core/interfaces.hpp"

#include <memory>

namespace lenses::ai::ort {

class OnnxMaskGenerator final : public lenses::core::IMaskGenerator {
public:
	OnnxMaskGenerator();
	~OnnxMaskGenerator() override;

	bool Start(const lenses::core::RuntimeConfig &config) override;
	void Stop() override;
	bool SubmitFrame(lenses::core::FrameTicket frame) override;
	std::optional<lenses::core::MaskFrame> TryPopMaskFrame() override;
	[[nodiscard]] lenses::core::MaskGeneratorStats GetStats() const override;
	[[nodiscard]] lenses::core::MaskGeneratorHealth GetHealth() const override;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace lenses::ai::ort
