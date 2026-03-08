#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lenses_autograde {

enum class RuntimeBackend : uint32_t {
	Deterministic = 0,
};

enum class RuntimeState : uint32_t {
	Disabled = 0,
	Idle = 1,
	Queued = 2,
	Running = 3,
	Ready = 4,
	Error = 5,
};

enum class RuntimeInputTransfer : uint32_t {
	SrgbNonlinear = 0,
	Linear = 1,
};

struct RuntimeConfig {
	bool enabled = false;
	uint32_t input_width = 256;
	uint32_t input_height = 256;
};

struct RuntimeStatus {
	RuntimeState state = RuntimeState::Disabled;
	RuntimeBackend backend = RuntimeBackend::Deterministic;
	bool busy = false;
	bool has_result = false;
	uint64_t generation = 0;
	double last_infer_ms = 0.0;
	double last_total_ms = 0.0;
	std::string detail = "Disabled";
};

struct RuntimeResult {
	std::vector<float> lut;
	float detail_amount = 0.0f;
	RuntimeBackend backend = RuntimeBackend::Deterministic;
	uint32_t lut_dimension = 33;
	uint64_t generation = 0;
	double infer_ms = 0.0;
	double total_ms = 0.0;
};

class AnalysisRuntime final {
public:
	AnalysisRuntime();
	~AnalysisRuntime();

	AnalysisRuntime(const AnalysisRuntime &) = delete;
	AnalysisRuntime &operator=(const AnalysisRuntime &) = delete;

	bool Configure(const RuntimeConfig &config);
	bool RequestAnalysis(uint64_t frame_id, uint32_t width, uint32_t height, uint32_t linesize,
			     const uint8_t *bgra, size_t bgra_size, uint64_t timestamp_ns,
			     RuntimeInputTransfer input_transfer);
	bool PollResult(RuntimeResult &out_result);
	RuntimeStatus GetStatus() const;
	bool IsBusy() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace lenses_autograde
