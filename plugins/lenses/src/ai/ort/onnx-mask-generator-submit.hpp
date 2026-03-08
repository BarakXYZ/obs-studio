#pragma once

#include "ai/ort/onnx-mask-generator-preprocess.hpp"
#include "ai/ort/onnx-mask-generator-stats.hpp"

#include "lenses/core/interfaces.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace lenses::ai::ort::detail {

struct SubmitRuntimeState {
	const lenses::core::RuntimeConfig *config = nullptr;
	std::deque<lenses::core::FrameTicket> *submit_queue = nullptr;
	lenses::core::MaskGeneratorStats *stats = nullptr;

	std::vector<uint8_t> *submit_similarity_prev_sample = nullptr;
	std::vector<uint8_t> *submit_similarity_scratch_sample = nullptr;
	uint32_t *submit_similarity_consecutive_skips = nullptr;

	RuntimeMetricsCollector *metrics_collector = nullptr;
};

bool HandleSubmitQueueCapacity(SubmitRuntimeState &state, uint64_t now_ns);

bool ShouldSkipBySimilarity(SubmitRuntimeState &state, const lenses::core::FrameTicket &frame);

void EnqueueSubmitFrame(SubmitRuntimeState &state, lenses::core::FrameTicket frame,
			uint64_t now_ns);

inline void RecordDroppedSubmitFrame(SubmitRuntimeState &state, uint64_t now_ns)
{
	if (!state.stats)
		return;

	state.stats->dropped_frames++;
	if (state.metrics_collector)
		state.metrics_collector->RecordDropEvent(now_ns);
}

inline bool HandleSubmitQueueCapacity(SubmitRuntimeState &state, uint64_t now_ns)
{
	if (!state.config || !state.submit_queue || !state.stats)
		return false;

	const size_t submit_limit = std::max<size_t>(1, state.config->submit_queue_limit);
	if (state.submit_queue->size() < submit_limit)
		return true;

	if (state.config->drop_policy == lenses::core::DropPolicy::DropOldest) {
		state.submit_queue->pop_front();
		RecordDroppedSubmitFrame(state, now_ns);
		return true;
	}

	RecordDroppedSubmitFrame(state, now_ns);
	state.stats->submit_queue_depth = state.submit_queue->size();
	return false;
}

inline bool ShouldSkipBySimilarity(SubmitRuntimeState &state,
				   const lenses::core::FrameTicket &frame)
{
	if (!state.config || !state.submit_queue || !state.stats ||
	    !state.submit_similarity_prev_sample || !state.submit_similarity_scratch_sample ||
	    !state.submit_similarity_consecutive_skips)
		return false;

	if (!state.config->enable_similarity_skip)
		return false;

	if (!IsFrameSimilar(frame, *state.submit_similarity_prev_sample,
			    *state.submit_similarity_scratch_sample,
			    std::max(0.0f, state.config->similarity_threshold), nullptr))
		return false;

	const uint32_t max_consecutive_similarity_skips =
		SimilaritySkipBudget(state.config->ai_fps_target);
	if (*state.submit_similarity_consecutive_skips >= max_consecutive_similarity_skips)
		return false;

	(*state.submit_similarity_consecutive_skips)++;
	state.stats->similarity_skipped_frames++;
	state.stats->reused_last_mask_frames++;
	state.stats->last_readback_ms = frame.readback_ms;
	state.stats->submit_queue_depth = state.submit_queue->size();
	return true;
}

inline void EnqueueSubmitFrame(SubmitRuntimeState &state, lenses::core::FrameTicket frame,
			       uint64_t now_ns)
{
	if (!state.submit_queue || !state.stats)
		return;

	frame.runtime_submit_ns = now_ns;
	state.submit_queue->push_back(std::move(frame));
	state.stats->submitted_frames++;
	if (state.metrics_collector)
		state.metrics_collector->RecordSubmitEvent(now_ns);
	state.stats->submit_queue_depth = state.submit_queue->size();
}

} // namespace lenses::ai::ort::detail
