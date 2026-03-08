#include "lenses-autograde/analysis-runtime.hpp"
#include "runtime/deterministic-grader.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lenses_autograde {
namespace {

constexpr uint32_t kLutDim = 33U;

const char *BackendLabel(RuntimeBackend backend)
{
	switch (backend) {
	case RuntimeBackend::Deterministic:
		return "deterministic";
	}
	return "unknown";
}

struct RuntimeConfigState {
	bool enabled = false;
	uint32_t input_width = 256;
	uint32_t input_height = 256;
};

struct FrameRequest {
	uint64_t frame_id = 0;
	uint64_t timestamp_ns = 0;
	uint64_t config_revision = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t linesize = 0;
	std::vector<uint8_t> bgra;
	RuntimeInputTransfer input_transfer = RuntimeInputTransfer::SrgbNonlinear;
};

} // namespace

class AnalysisRuntime::Impl final {
public:
	Impl()
	{
		status_.state = RuntimeState::Disabled;
		status_.backend = RuntimeBackend::Deterministic;
		status_.detail = "Disabled (deterministic)";
		worker_ = std::thread(&Impl::WorkerLoop, this);
	}

	~Impl()
	{
		{
			std::scoped_lock lock(mutex_);
			stop_ = true;
		}
		cv_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

	bool Configure(const RuntimeConfig &config)
	{
		std::scoped_lock lock(mutex_);
		const bool changed = ApplyConfigLocked(config);
		if (changed) {
			config_revision_ += 1;
			if (request_ready_) {
				request_ready_ = false;
				request_ = {};
			}
		}

		if (!config_.enabled) {
			status_.state = RuntimeState::Disabled;
			status_.backend = RuntimeBackend::Deterministic;
			status_.busy = false;
			status_.detail = "Disabled (deterministic)";
			request_ready_ = false;
			request_ = {};
		} else if (status_.state == RuntimeState::Disabled) {
			status_.state = RuntimeState::Idle;
			status_.backend = RuntimeBackend::Deterministic;
			status_.detail = "Idle (deterministic)";
		} else if (changed && status_.state == RuntimeState::Idle) {
			status_.detail = "Idle (deterministic)";
		}

		return true;
	}

	bool RequestAnalysis(uint64_t frame_id, uint32_t width, uint32_t height, uint32_t linesize,
			     const uint8_t *bgra, size_t bgra_size, uint64_t timestamp_ns,
			     RuntimeInputTransfer input_transfer)
	{
		if (!bgra || bgra_size == 0 || width == 0 || height == 0 || linesize < width * 4U)
			return false;

		std::scoped_lock lock(mutex_);
		if (!config_.enabled || processing_ || request_ready_)
			return false;

		FrameRequest next{};
		next.frame_id = frame_id;
		next.timestamp_ns = timestamp_ns;
		next.config_revision = config_revision_;
		next.width = width;
		next.height = height;
		next.linesize = linesize;
		next.input_transfer = input_transfer;
		next.bgra.resize(bgra_size);
		memcpy(next.bgra.data(), bgra, bgra_size);

		request_ = std::move(next);
		request_ready_ = true;
		status_.state = RuntimeState::Queued;
		status_.backend = RuntimeBackend::Deterministic;
		status_.busy = true;
		status_.detail = "Queued (deterministic)";
		cv_.notify_all();
		return true;
	}

	bool PollResult(RuntimeResult &out_result)
	{
		out_result = {};

		std::scoped_lock lock(mutex_);
		if (!result_ready_for_host_ || latest_lut_.empty())
			return false;

		out_result.lut = latest_lut_;
		out_result.detail_amount = latest_detail_amount_;
		out_result.backend = RuntimeBackend::Deterministic;
		out_result.lut_dimension = kLutDim;
		out_result.generation = status_.generation;
		out_result.infer_ms = 0.0;
		out_result.total_ms = status_.last_total_ms;
		result_ready_for_host_ = false;
		return true;
	}

	RuntimeStatus GetStatus() const
	{
		std::scoped_lock lock(mutex_);
		return status_;
	}

	bool IsBusy() const
	{
		std::scoped_lock lock(mutex_);
		return processing_ || request_ready_;
	}

private:
	bool ApplyConfigLocked(const RuntimeConfig &config)
	{
		RuntimeConfigState next{};
		next.enabled = config.enabled;
		next.input_width = config.input_width > 0 ? config.input_width : 256;
		next.input_height = config.input_height > 0 ? config.input_height : 256;

		const bool changed = next.enabled != config_.enabled ||
				     next.input_width != config_.input_width ||
				     next.input_height != config_.input_height;
		config_ = std::move(next);
		return changed;
	}

	bool RunRequest(const FrameRequest &request, const RuntimeConfigState &config,
			std::vector<float> &out_lut, float &out_detail_amount, double &out_infer_ms,
			std::string &detail_out)
	{
		if (!config.enabled) {
			detail_out = "Autograde disabled";
			return false;
		}

		out_detail_amount = 0.0f;
		out_infer_ms = 0.0;
		DeterministicGradeOutput grade{};
		const DeterministicInputTransferHint transfer_hint =
			request.input_transfer == RuntimeInputTransfer::Linear
				? DeterministicInputTransferHint::Linear
				: DeterministicInputTransferHint::SrgbNonlinear;
		const bool ok = BuildDeterministicGradeFromBgra(request.bgra.data(), request.width,
								 request.height, request.linesize, kLutDim,
								 transfer_hint, grade);
		if (ok) {
			out_lut = std::move(grade.lut);
			out_detail_amount = grade.detail_amount;
			detail_out = std::move(grade.detail);
		} else {
			detail_out = std::move(grade.detail);
		}
		return ok;
	}

	void WorkerLoop()
	{
		for (;;) {
			FrameRequest request;
			RuntimeConfigState config;
			uint64_t active_config_revision = 0;
			{
				std::unique_lock lock(mutex_);
				cv_.wait(lock, [&]() { return stop_ || request_ready_; });
				if (stop_)
					return;

				request = std::move(request_);
				request_ready_ = false;
				processing_ = true;
				status_.busy = true;
				status_.state = RuntimeState::Running;
				status_.backend = RuntimeBackend::Deterministic;
				status_.detail = "Running (deterministic)";
				config = config_;
				active_config_revision = config_revision_;
			}

			if (request.config_revision != active_config_revision) {
				std::scoped_lock lock(mutex_);
				processing_ = false;
				status_.busy = request_ready_;
				status_.backend = RuntimeBackend::Deterministic;
				status_.state = config_.enabled ? RuntimeState::Idle : RuntimeState::Disabled;
				status_.detail = "Request superseded by config change";
				continue;
			}

			const auto started = std::chrono::steady_clock::now();
			std::vector<float> lut;
			float detail_amount = 0.0f;
			double infer_ms = 0.0;
			std::string detail;
			const bool ok = RunRequest(request, config, lut, detail_amount, infer_ms, detail);
			const auto finished = std::chrono::steady_clock::now();
			const double total_ms =
				std::chrono::duration<double, std::milli>(finished - started).count();

			{
				std::scoped_lock lock(mutex_);
				processing_ = false;
				status_.busy = request_ready_;
				status_.backend = RuntimeBackend::Deterministic;

				const bool stale_result = request.config_revision != config_revision_;
				if (stale_result) {
					status_.state = config_.enabled ? RuntimeState::Idle : RuntimeState::Disabled;
					status_.detail = "Result discarded: config changed";
				} else if (ok) {
					latest_lut_ = std::move(lut);
					latest_detail_amount_ = detail_amount;
					result_ready_for_host_ = true;
					status_.has_result = true;
					status_.generation += 1;
					status_.last_infer_ms = infer_ms;
					status_.last_total_ms = total_ms;
					status_.state = RuntimeState::Ready;
					char summary[512] = {};
					(void)snprintf(summary, sizeof(summary),
						       "Ready (%s, total=%.2f ms) %s",
						       BackendLabel(RuntimeBackend::Deterministic), total_ms,
						       detail.c_str());
					status_.detail = summary;
				} else {
					status_.state = RuntimeState::Error;
					status_.detail = detail;
				}
			}
		}
	}

private:
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::thread worker_;
	bool stop_ = false;
	bool request_ready_ = false;
	bool processing_ = false;
	bool result_ready_for_host_ = false;
	uint64_t config_revision_ = 1;

	RuntimeConfigState config_{};
	FrameRequest request_{};
	std::vector<float> latest_lut_{};
	float latest_detail_amount_ = 0.0f;
	RuntimeStatus status_{};
};

AnalysisRuntime::AnalysisRuntime() : impl_(std::make_unique<Impl>()) {}

AnalysisRuntime::~AnalysisRuntime() = default;

bool AnalysisRuntime::Configure(const RuntimeConfig &config)
{
	return impl_->Configure(config);
}

bool AnalysisRuntime::RequestAnalysis(uint64_t frame_id, uint32_t width, uint32_t height, uint32_t linesize,
				      const uint8_t *bgra, size_t bgra_size,
				      uint64_t timestamp_ns, RuntimeInputTransfer input_transfer)
{
	return impl_->RequestAnalysis(frame_id, width, height, linesize, bgra, bgra_size,
				      timestamp_ns, input_transfer);
}

bool AnalysisRuntime::PollResult(RuntimeResult &out_result)
{
	return impl_->PollResult(out_result);
}

RuntimeStatus AnalysisRuntime::GetStatus() const
{
	return impl_->GetStatus();
}

bool AnalysisRuntime::IsBusy() const
{
	return impl_->IsBusy();
}

} // namespace lenses_autograde
