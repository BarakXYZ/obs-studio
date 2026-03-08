#pragma once

#include "ai/ort/onnx-mask-generator-iobinding.hpp"

#include "lenses/core/interfaces.hpp"

#if defined(LENSES_ENABLE_ORT)

#include <onnxruntime_cxx_api.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace lenses::ai::ort::detail {

struct SessionModelIoInfo {
	std::vector<std::string> input_name_storage;
	std::vector<std::string> output_name_storage;
	std::string selected_input_name;
	size_t detection_output_index = std::numeric_limits<size_t>::max();
	size_t proto_output_index = std::numeric_limits<size_t>::max();
};

struct SessionRuntimeIoState {
	std::vector<std::string> *input_name_storage = nullptr;
	std::vector<std::string> *output_name_storage = nullptr;
	std::string *selected_input_name = nullptr;
	size_t *detection_output_index = nullptr;
	size_t *proto_output_index = nullptr;
	std::vector<const char *> *input_names = nullptr;
	std::vector<const char *> *output_names = nullptr;
	uint32_t *input_width = nullptr;
	uint32_t *input_height = nullptr;
};

void BuildNameViews(const std::vector<std::string> &storage, std::vector<const char *> &views);

bool InitializeSessionModelIo(Ort::Session &session, const lenses::core::RuntimeConfig &config,
			      SessionModelIoInfo &out, std::string &error_out);
bool LoadSessionModelIoRuntime(Ort::Session &session, const lenses::core::RuntimeConfig &config,
			       SessionRuntimeIoState &state, uint32_t max_input_dimension,
			       std::string &error_out);

struct SessionOrchestrationContext {
	const lenses::core::RuntimeConfig *config = nullptr;
	std::unique_ptr<Ort::Env> *env = nullptr;
	std::unique_ptr<Ort::Session> *session = nullptr;
	OrtLoggingFunction log_callback = nullptr;
	void *log_callback_user_data = nullptr;

	std::string *active_execution_provider = nullptr;
	bool *coreml_requested = nullptr;
	bool *coreml_enabled = nullptr;
	bool *cpu_ep_fallback_disabled = nullptr;

	SessionRuntimeIoState session_runtime_io_state{};
	SessionRuntimeState session_runtime_state{};
	IoBindingRuntimeState io_binding_runtime_state{};
	uint32_t max_input_dimension = 4096;
};

bool InitializeOrtSessionRuntime(SessionOrchestrationContext &context,
				 std::string &error_out);

} // namespace lenses::ai::ort::detail

#endif
