#pragma once

#include "lenses/core/interfaces.hpp"

#if defined(LENSES_ENABLE_ORT)

#include <onnxruntime_c_api.h>
#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>

namespace lenses::ai::ort::detail {

struct CreateOrtSessionContext {
	const lenses::core::RuntimeConfig *config = nullptr;
	std::unique_ptr<Ort::Env> *env = nullptr;
	std::unique_ptr<Ort::Session> *session = nullptr;
	OrtLoggingFunction log_callback = nullptr;
	void *log_callback_user_data = nullptr;
};

struct CreateOrtSessionResult {
	std::string configured_intra_spinning = "default";
	std::string configured_inter_spinning = "default";
	std::string active_execution_provider = "cpu";
	bool coreml_requested = false;
	bool coreml_enabled = false;
	bool cpu_ep_fallback_disabled = false;
};

bool CreateOrtSession(CreateOrtSessionContext &context, CreateOrtSessionResult &result,
		      std::string &error_out);

} // namespace lenses::ai::ort::detail

#endif
