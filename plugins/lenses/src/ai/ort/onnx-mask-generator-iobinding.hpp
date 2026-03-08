#pragma once

#if defined(LENSES_ENABLE_ORT)

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lenses::ai::ort::detail {

struct IoBindingSetupResult {
	bool io_binding_static_outputs = false;
	bool io_binding_dynamic_outputs = false;
	std::unique_ptr<Ort::IoBinding> io_binding;
	std::unique_ptr<Ort::MemoryInfo> io_binding_output_memory_info;
	std::vector<int64_t> bound_detection_shape;
	std::vector<int64_t> bound_proto_shape;
	std::vector<float> bound_detection_storage;
	std::vector<float> bound_proto_storage;
	std::unique_ptr<Ort::Value> bound_detection_output;
	std::unique_ptr<Ort::Value> bound_proto_output;
};

struct IoBindingRuntimeState {
	Ort::Session *session = nullptr;
	size_t detection_output_index = 0;
	size_t proto_output_index = 0;
	const std::vector<std::string> *output_name_storage = nullptr;
	std::unique_ptr<Ort::IoBinding> *io_binding = nullptr;
	bool *io_binding_static_outputs = nullptr;
	bool *io_binding_dynamic_outputs = nullptr;
	std::unique_ptr<Ort::MemoryInfo> *io_binding_output_memory_info = nullptr;
	std::vector<int64_t> *bound_detection_shape = nullptr;
	std::vector<int64_t> *bound_proto_shape = nullptr;
	std::vector<float> *bound_detection_storage = nullptr;
	std::vector<float> *bound_proto_storage = nullptr;
	std::unique_ptr<Ort::Value> *bound_detection_output = nullptr;
	std::unique_ptr<Ort::Value> *bound_proto_output = nullptr;
};

struct SessionRuntimeState {
	uint32_t *mask_dim = nullptr;
	uint32_t *proto_width = nullptr;
	uint32_t *proto_height = nullptr;
	bool *proto_channel_first = nullptr;
	bool *io_binding_enabled = nullptr;
	IoBindingRuntimeState io_binding{};
};

bool InitializeIoBindingSetup(Ort::Session &session, size_t detection_output_index,
			      size_t proto_output_index,
			      const std::vector<std::string> &output_name_storage,
			      IoBindingSetupResult &out, std::string &detail_out);
void ClearIoBindingRuntime(IoBindingRuntimeState &state);
void ResetSessionRuntimeState(SessionRuntimeState &state);
bool InitializeIoBindingRuntime(IoBindingRuntimeState &state, std::string &detail_out);

} // namespace lenses::ai::ort::detail

#endif
