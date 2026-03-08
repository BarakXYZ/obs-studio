#pragma once

#include "lenses/core/interfaces.hpp"

#if defined(LENSES_ENABLE_ORT)

#include <onnxruntime_cxx_api.h>

#include <string>

namespace lenses::ai::ort::detail {

struct ProviderSetupResult {
	bool success = true;
	bool used_fallback = false;
	bool coreml_requested = false;
	bool coreml_enabled = false;
	bool xnnpack_enabled = false;
	bool cpu_ep_fallback_disabled = false;
	std::string active_execution_provider = "cpu";
	std::string configured_intra_spinning = "default";
	std::string configured_inter_spinning = "default";
	std::string error_detail;
};

ProviderSetupResult ConfigureExecutionProviders(Ort::SessionOptions &options,
					const lenses::core::RuntimeConfig &config);
const char *CpuThreadingModeDescription(const std::string &execution_provider);
void LogSessionOptionsSummary(const lenses::core::RuntimeConfig &config,
			      const std::string &configured_intra_spinning,
			      const std::string &configured_inter_spinning,
			      bool cpu_ep_fallback_disabled);

} // namespace lenses::ai::ort::detail

#endif
