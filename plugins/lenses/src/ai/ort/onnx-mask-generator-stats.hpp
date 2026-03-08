#pragma once

#include "ai/ort/onnx-mask-generator-preprocess.hpp"

#include "lenses/core/interfaces.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace lenses::ai::ort::detail {

constexpr size_t kStageHistoryCapacity = 256;
constexpr uint64_t kFpsWindowNs = 1000000000ULL;

struct StageHistory {
	std::array<double, kStageHistoryCapacity> values{};
	size_t count = 0;
	size_t cursor = 0;

	void Push(double value)
	{
		values[cursor] = value;
		cursor = (cursor + 1U) % values.size();
		if (count < values.size())
			count++;
	}

	std::vector<double> SnapshotSorted() const
	{
		std::vector<double> snapshot;
		snapshot.reserve(count);
		for (size_t i = 0; i < count; ++i)
			snapshot.push_back(values[i]);
		std::sort(snapshot.begin(), snapshot.end());
		return snapshot;
	}
};

inline void RecordEvent(std::deque<uint64_t> &events, uint64_t now_ns)
{
	events.push_back(now_ns);
	const uint64_t trim_before = now_ns > (kFpsWindowNs * 4U) ? (now_ns - (kFpsWindowNs * 4U)) : 0;
	while (!events.empty() && events.front() < trim_before)
		events.pop_front();
}

inline double ComputeFpsFromEvents(const std::deque<uint64_t> &events, uint64_t now_ns)
{
	if (events.empty())
		return 0.0;
	size_t count = 0;
	for (auto it = events.rbegin(); it != events.rend(); ++it) {
		if (now_ns >= *it && (now_ns - *it) <= kFpsWindowNs) {
			++count;
			continue;
		}
		if (now_ns < *it)
			continue;
		break;
	}
	return (double)count;
}

inline void FillPercentiles(const StageHistory &history, double *p50, double *p95, double *p99)
{
	if (p50)
		*p50 = 0.0;
	if (p95)
		*p95 = 0.0;
	if (p99)
		*p99 = 0.0;
	const std::vector<double> sorted = history.SnapshotSorted();
	if (sorted.empty())
		return;
	if (p50)
		*p50 = PercentileFromSamples(sorted, 50.0);
	if (p95)
		*p95 = PercentileFromSamples(sorted, 95.0);
	if (p99)
		*p99 = PercentileFromSamples(sorted, 99.0);
}

class RuntimeMetricsCollector {
public:
	void Reset()
	{
		submit_event_times_ns_.clear();
		complete_event_times_ns_.clear();
		drop_event_times_ns_.clear();
		readback_history_ = {};
		preprocess_history_ = {};
		infer_history_ = {};
		decode_history_ = {};
		track_history_ = {};
		queue_latency_history_ = {};
		end_to_end_latency_history_ = {};
	}

	void RecordSubmitEvent(uint64_t now_ns) { RecordEvent(submit_event_times_ns_, now_ns); }

	void RecordCompleteEvent(uint64_t now_ns) { RecordEvent(complete_event_times_ns_, now_ns); }

	void RecordDropEvent(uint64_t now_ns) { RecordEvent(drop_event_times_ns_, now_ns); }

	void RecordStageTimings(double readback_ms, double preprocess_ms, double infer_ms,
				double decode_ms, double track_ms)
	{
		readback_history_.Push(readback_ms);
		preprocess_history_.Push(preprocess_ms);
		infer_history_.Push(infer_ms);
		decode_history_.Push(decode_ms);
		track_history_.Push(track_ms);
	}

	void RecordQueueLatency(double queue_latency_ms)
	{
		queue_latency_history_.Push(std::max(0.0, queue_latency_ms));
	}

	void RecordEndToEndLatency(double end_to_end_latency_ms)
	{
		end_to_end_latency_history_.Push(std::max(0.0, end_to_end_latency_ms));
	}

	void Snapshot(lenses::core::MaskGeneratorStats &stats, uint64_t now_ns,
		      size_t submit_queue_depth, size_t output_queue_depth) const
	{
		stats.submit_fps = ComputeFpsFromEvents(submit_event_times_ns_, now_ns);
		stats.complete_fps = ComputeFpsFromEvents(complete_event_times_ns_, now_ns);
		stats.drop_fps = ComputeFpsFromEvents(drop_event_times_ns_, now_ns);

		FillPercentiles(readback_history_, &stats.readback_ms_p50, &stats.readback_ms_p95,
				&stats.readback_ms_p99);
		FillPercentiles(preprocess_history_, &stats.preprocess_ms_p50,
				&stats.preprocess_ms_p95, &stats.preprocess_ms_p99);
		FillPercentiles(infer_history_, &stats.infer_ms_p50, &stats.infer_ms_p95,
				&stats.infer_ms_p99);
		FillPercentiles(decode_history_, &stats.decode_ms_p50, &stats.decode_ms_p95,
				&stats.decode_ms_p99);
		FillPercentiles(track_history_, &stats.track_ms_p50, &stats.track_ms_p95,
				&stats.track_ms_p99);
		FillPercentiles(queue_latency_history_, &stats.queue_latency_ms_p50,
				&stats.queue_latency_ms_p95, &stats.queue_latency_ms_p99);
		FillPercentiles(end_to_end_latency_history_,
				&stats.end_to_end_latency_ms_p50,
				&stats.end_to_end_latency_ms_p95,
				&stats.end_to_end_latency_ms_p99);

		stats.submit_queue_depth = submit_queue_depth;
		stats.output_queue_depth = output_queue_depth;
	}

private:
	std::deque<uint64_t> submit_event_times_ns_{};
	std::deque<uint64_t> complete_event_times_ns_{};
	std::deque<uint64_t> drop_event_times_ns_{};
	StageHistory readback_history_{};
	StageHistory preprocess_history_{};
	StageHistory infer_history_{};
	StageHistory decode_history_{};
	StageHistory track_history_{};
	StageHistory queue_latency_history_{};
	StageHistory end_to_end_latency_history_{};
};

inline void ApplyQueueDepthStats(lenses::core::MaskGeneratorStats &stats,
				 size_t submit_queue_depth, size_t output_queue_depth)
{
	stats.submit_queue_depth = submit_queue_depth;
	stats.output_queue_depth = output_queue_depth;
}

} // namespace lenses::ai::ort::detail
