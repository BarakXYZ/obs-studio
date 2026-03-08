#include "lenses/ai/tracking/bytetrack-tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lenses::ai::tracking {

namespace {

float Clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

lenses::core::NormalizedRect NormalizeRect(lenses::core::NormalizedRect rect)
{
	rect.x = Clamp01(rect.x);
	rect.y = Clamp01(rect.y);
	rect.width = Clamp01(rect.width);
	rect.height = Clamp01(rect.height);
	if (rect.x + rect.width > 1.0f)
		rect.width = std::max(0.0f, 1.0f - rect.x);
	if (rect.y + rect.height > 1.0f)
		rect.height = std::max(0.0f, 1.0f - rect.y);
	return rect;
}

float IoU(const lenses::core::NormalizedRect &a_raw, const lenses::core::NormalizedRect &b_raw)
{
	const auto a = NormalizeRect(a_raw);
	const auto b = NormalizeRect(b_raw);

	const float ax2 = a.x + a.width;
	const float ay2 = a.y + a.height;
	const float bx2 = b.x + b.width;
	const float by2 = b.y + b.height;

	const float ix1 = std::max(a.x, b.x);
	const float iy1 = std::max(a.y, b.y);
	const float ix2 = std::min(ax2, bx2);
	const float iy2 = std::min(ay2, by2);
	const float iw = std::max(0.0f, ix2 - ix1);
	const float ih = std::max(0.0f, iy2 - iy1);
	const float intersection = iw * ih;
	if (intersection <= 0.0f)
		return 0.0f;

	const float a_area = a.width * a.height;
	const float b_area = b.width * b.height;
	const float union_area = a_area + b_area - intersection;
	if (union_area <= 0.0f)
		return 0.0f;

	return intersection / union_area;
}

struct MatchCandidate {
	size_t track_index = 0;
	size_t detection_index = 0;
	float iou = 0.0f;
	uint64_t track_id = 0;
};

} // namespace

ByteTrackTracker::ByteTrackTracker(ByteTrackConfig config) : config_(config)
{
	if (config_.track_low_thresh > config_.track_high_thresh)
		config_.track_low_thresh = config_.track_high_thresh;
	if (config_.new_track_thresh < config_.track_high_thresh)
		config_.new_track_thresh = config_.track_high_thresh;
}

void ByteTrackTracker::Reset()
{
	tracks_.clear();
	next_track_id_ = 1;
	frame_counter_ = 0;
}

void ByteTrackTracker::Update(lenses::core::MaskFrame &frame)
{
	frame_counter_++;
	std::vector<uint64_t> assigned_ids(frame.instances.size(), 0);
	std::vector<bool> track_matched(tracks_.size(), false);
	std::vector<bool> detection_matched(frame.instances.size(), false);

	std::vector<size_t> high_detection_indices;
	std::vector<size_t> low_detection_indices;
	high_detection_indices.reserve(frame.instances.size());
	low_detection_indices.reserve(frame.instances.size());

	for (size_t i = 0; i < frame.instances.size(); ++i) {
		const auto &instance = frame.instances[i];
		if (instance.confidence >= config_.track_high_thresh) {
			high_detection_indices.push_back(i);
		} else if (instance.confidence >= config_.track_low_thresh) {
			low_detection_indices.push_back(i);
		}
	}

	auto run_match_stage = [&](const std::vector<size_t> &detection_indices) {
		std::vector<MatchCandidate> candidates;
		for (size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
			const auto &track = tracks_[track_index];
			if (track.lost_frames > config_.track_buffer || track_matched[track_index])
				continue;

			for (size_t detection_index : detection_indices) {
				if (detection_matched[detection_index])
					continue;
				const auto &instance = frame.instances[detection_index];
				if (config_.class_aware && track.class_id != instance.class_id)
					continue;

				const float iou = IoU(track.bbox, instance.bbox_norm);
				if (iou < config_.match_thresh)
					continue;

				candidates.push_back({
					.track_index = track_index,
					.detection_index = detection_index,
					.iou = iou,
					.track_id = track.track_id,
				});
			}
		}

		std::sort(candidates.begin(), candidates.end(), [](const auto &lhs, const auto &rhs) {
			if (lhs.iou != rhs.iou)
				return lhs.iou > rhs.iou;
			if (lhs.track_id != rhs.track_id)
				return lhs.track_id < rhs.track_id;
			return lhs.detection_index < rhs.detection_index;
		});

		for (const auto &candidate : candidates) {
			if (track_matched[candidate.track_index] || detection_matched[candidate.detection_index])
				continue;

			auto &track = tracks_[candidate.track_index];
			const auto &instance = frame.instances[candidate.detection_index];
			track.bbox = instance.bbox_norm;
			track.class_id = instance.class_id;
			track.confidence = instance.confidence;
			track.last_seen_frame = frame_counter_;
			track.lost_frames = 0;

			assigned_ids[candidate.detection_index] = track.track_id;
			track_matched[candidate.track_index] = true;
			detection_matched[candidate.detection_index] = true;
		}
	};

	run_match_stage(high_detection_indices);
	run_match_stage(low_detection_indices);

	for (size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
		if (track_matched[track_index])
			continue;
		tracks_[track_index].lost_frames++;
	}

	for (size_t detection_index : high_detection_indices) {
		if (detection_matched[detection_index])
			continue;
		const auto &instance = frame.instances[detection_index];
		if (instance.confidence < config_.new_track_thresh)
			continue;

		TrackState state{};
		state.track_id = next_track_id_++;
		state.class_id = instance.class_id;
		state.confidence = instance.confidence;
		state.bbox = instance.bbox_norm;
		state.last_seen_frame = frame_counter_;
		state.lost_frames = 0;
		tracks_.push_back(state);

		assigned_ids[detection_index] = state.track_id;
		detection_matched[detection_index] = true;
	}

	tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [this](const TrackState &track) {
		return track.lost_frames > config_.track_buffer;
	}),
	      tracks_.end());

	for (size_t i = 0; i < frame.instances.size(); ++i) {
		if (assigned_ids[i] == 0)
			assigned_ids[i] = frame.instances[i].track_id;
		frame.instances[i].track_id = assigned_ids[i];
	}
}

} // namespace lenses::ai::tracking
