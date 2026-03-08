#include "lenses/ai/cloud/cloud-mask-generator.hpp"

#include <utility>

namespace lenses::ai::cloud {

CloudMaskGenerator::CloudMaskGenerator(std::unique_ptr<lenses::core::IMaskGenerator> local_fallback)
	: fallback_(std::move(local_fallback))
{
}

CloudMaskGenerator::~CloudMaskGenerator()
{
	Stop();
}

bool CloudMaskGenerator::Start(const lenses::core::RuntimeConfig &config)
{
	std::scoped_lock lock(mutex_);
	if (!fallback_)
		return false;

	config_ = config;
	cloud_stats_ = {};
	running_ = true;
	return fallback_->Start(config_);
}

void CloudMaskGenerator::Stop()
{
	std::scoped_lock lock(mutex_);
	running_ = false;
	if (fallback_)
		fallback_->Stop();
}

bool CloudMaskGenerator::SubmitFrame(lenses::core::FrameTicket frame)
{
	std::scoped_lock lock(mutex_);
	if (!running_ || !fallback_)
		return false;

	return fallback_->SubmitFrame(std::move(frame));
}

std::optional<lenses::core::MaskFrame> CloudMaskGenerator::TryPopMaskFrame()
{
	std::scoped_lock lock(mutex_);
	if (!running_ || !fallback_)
		return std::nullopt;

	// Phase-1 cloud scaffold: if no cloud response arrives before timeout,
	// the local backend result is used under the same MaskFrame contract.
	auto frame = fallback_->TryPopMaskFrame();
	if (!frame)
		return std::nullopt;

	cloud_stats_.cloud_timeout_frames++;
	cloud_stats_.cloud_fallback_frames++;
	cloud_stats_.last_latency_ms = frame->latency_ms;
	return frame;
}

lenses::core::MaskGeneratorStats CloudMaskGenerator::GetStats() const
{
	std::scoped_lock lock(mutex_);
	if (!fallback_)
		return cloud_stats_;

	lenses::core::MaskGeneratorStats stats = fallback_->GetStats();
	stats.cloud_timeout_frames += cloud_stats_.cloud_timeout_frames;
	stats.cloud_fallback_frames += cloud_stats_.cloud_fallback_frames;
	return stats;
}

lenses::core::MaskGeneratorHealth CloudMaskGenerator::GetHealth() const
{
	std::scoped_lock lock(mutex_);

	lenses::core::MaskGeneratorHealth health{};
	health.backend = "cloud";
	if (!fallback_) {
		health.ready = false;
		health.fallback_active = true;
		health.detail = "Cloud backend unavailable: local fallback generator is missing";
		return health;
	}

	const auto fallback_health = fallback_->GetHealth();
	health.ready = running_ && fallback_health.ready;
	health.fallback_active = true;
	health.detail = "Cloud scaffold active (local fallback path): " + fallback_health.detail;
	return health;
}

} // namespace lenses::ai::cloud
