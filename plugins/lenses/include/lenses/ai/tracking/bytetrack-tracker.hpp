#pragma once

#include "lenses/core/interfaces.hpp"

#include <cstdint>
#include <vector>

namespace lenses::ai::tracking {

struct ByteTrackConfig {
	float track_high_thresh = 0.6f;
	float track_low_thresh = 0.1f;
	float new_track_thresh = 0.6f;
	float match_thresh = 0.3f;
	uint32_t track_buffer = 30;
	bool class_aware = true;
};

class ByteTrackTracker final : public lenses::core::IInstanceTracker {
public:
	explicit ByteTrackTracker(ByteTrackConfig config = {});

	void Update(lenses::core::MaskFrame &frame) override;
	void Reset();

private:
	struct TrackState {
		uint64_t track_id = 0;
		int32_t class_id = -1;
		float confidence = 0.0f;
		lenses::core::NormalizedRect bbox{};
		uint64_t last_seen_frame = 0;
		uint32_t lost_frames = 0;
	};

	ByteTrackConfig config_{};
	uint64_t next_track_id_ = 1;
	uint64_t frame_counter_ = 0;
	std::vector<TrackState> tracks_;
};

} // namespace lenses::ai::tracking
