#include "lenses/core/noop-mask-generator.hpp"

#include "lenses/ai/tracking/bytetrack-tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace lenses::core {

namespace {

MaskFrame BuildSyntheticMaskFrame(const FrameTicket &frame)
{
	MaskFrame mask_frame{};
	mask_frame.frame_id = frame.frame_id;
	mask_frame.source_width = frame.source_width;
	mask_frame.source_height = frame.source_height;
	mask_frame.timestamp_ns = frame.timestamp_ns;

	const float phase = (float)(frame.frame_id % 240ULL) / 239.0f;
	const bool person_occluded = ((frame.frame_id % 90ULL) >= 70ULL && (frame.frame_id % 90ULL) < 75ULL);

	if (!person_occluded) {
		MaskInstance person{};
		person.class_id = 0;
		person.confidence = 0.91f;
		person.bbox_norm.x = 0.12f + phase * 0.42f;
		person.bbox_norm.y = 0.12f;
		person.bbox_norm.width = 0.14f;
		person.bbox_norm.height = 0.34f;
		person.timestamp_ns = frame.timestamp_ns;
		mask_frame.instances.push_back(person);
	}

	MaskInstance car{};
	car.class_id = 2;
	car.confidence = 0.88f;
	car.bbox_norm.x = 0.62f - phase * 0.38f;
	car.bbox_norm.y = 0.62f;
	car.bbox_norm.width = 0.28f;
	car.bbox_norm.height = 0.18f;
	car.timestamp_ns = frame.timestamp_ns;
	mask_frame.instances.push_back(car);

	return mask_frame;
}

} // namespace

NoopMaskGenerator::~NoopMaskGenerator()
{
	Stop();
}

bool NoopMaskGenerator::Start(const RuntimeConfig &config)
{
	Stop();

	std::scoped_lock lock(mutex_);
	config_ = config;
	running_ = true;
	stop_requested_ = false;
	stats_ = {};
	submit_queue_.clear();
	output_queue_.clear();
	lenses::ai::tracking::ByteTrackConfig tracker_config{};
	tracker_config.track_buffer = std::max<uint32_t>(12U, config.ai_fps_target * 2U);
	tracker_ = std::make_unique<lenses::ai::tracking::ByteTrackTracker>(tracker_config);
	worker_ = std::thread(&NoopMaskGenerator::WorkerLoop, this);
	return true;
}

void NoopMaskGenerator::Stop()
{
	{
		std::scoped_lock lock(mutex_);
		if (!running_ && !worker_.joinable())
			return;

		stop_requested_ = true;
		running_ = false;
	}

	cv_.notify_all();
	if (worker_.joinable())
		worker_.join();

	std::scoped_lock lock(mutex_);
	submit_queue_.clear();
	output_queue_.clear();
	tracker_.reset();
}

bool NoopMaskGenerator::SubmitFrame(FrameTicket frame)
{
	std::scoped_lock lock(mutex_);
	if (!running_)
		return false;

	stats_.submitted_frames++;
	const size_t submit_limit = std::max<size_t>(1, config_.submit_queue_limit);
	if (submit_queue_.size() >= submit_limit) {
		submit_queue_.pop_front();
		stats_.dropped_frames++;
	}

	submit_queue_.push_back(std::move(frame));
	stats_.submit_queue_depth = submit_queue_.size();
	cv_.notify_one();
	return true;
}

std::optional<MaskFrame> NoopMaskGenerator::TryPopMaskFrame()
{
	std::scoped_lock lock(mutex_);
	if (!output_queue_.empty()) {
		MaskFrame frame = std::move(output_queue_.front());
		output_queue_.pop_front();
		stats_.output_queue_depth = output_queue_.size();
		return frame;
	}

	return std::nullopt;
}

MaskGeneratorStats NoopMaskGenerator::GetStats() const
{
	std::scoped_lock lock(mutex_);
	MaskGeneratorStats stats = stats_;
	stats.submit_queue_depth = submit_queue_.size();
	stats.output_queue_depth = output_queue_.size();
	return stats;
}

MaskGeneratorHealth NoopMaskGenerator::GetHealth() const
{
	std::scoped_lock lock(mutex_);
	MaskGeneratorHealth health{};
	health.ready = running_;
	health.fallback_active = false;
	health.backend = "noop";
	health.detail = running_ ? "Synthetic mask generator active"
				 : "Synthetic mask generator idle";
	return health;
}

void NoopMaskGenerator::WorkerLoop()
{
	const uint32_t fps = std::max<uint32_t>(1, config_.ai_fps_target);
	const auto frame_interval = std::chrono::milliseconds(1000 / fps);

	for (;;) {
		FrameTicket frame{};
		{
			std::unique_lock lock(mutex_);
			cv_.wait(lock, [this] { return stop_requested_ || !submit_queue_.empty(); });
			if (stop_requested_)
				return;

			frame = submit_queue_.front();
			submit_queue_.pop_front();
			stats_.submit_queue_depth = submit_queue_.size();
		}

		auto started = std::chrono::steady_clock::now();
		std::this_thread::sleep_for(frame_interval);

		MaskFrame mask_frame = BuildSyntheticMaskFrame(frame);
		if (tracker_)
			tracker_->Update(mask_frame);
		mask_frame.latency_ms =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

		{
			std::scoped_lock lock(mutex_);
			const size_t output_limit = std::max<size_t>(1, config_.output_queue_limit);
			if (output_queue_.size() >= output_limit) {
				output_queue_.pop_front();
				stats_.dropped_frames++;
			}

			output_queue_.push_back(std::move(mask_frame));
			stats_.completed_frames++;
			stats_.last_latency_ms = output_queue_.back().latency_ms;
			stats_.output_queue_depth = output_queue_.size();
		}
	}
}

} // namespace lenses::core
