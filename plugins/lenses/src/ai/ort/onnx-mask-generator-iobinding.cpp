#include "ai/ort/onnx-mask-generator-iobinding.hpp"

#if defined(LENSES_ENABLE_ORT)

namespace lenses::ai::ort::detail {

namespace {

template<typename TensorInfoT>
bool TryGetTensorShape(const TensorInfoT &info, std::vector<int64_t> &shape)
{
	try {
		shape = info.GetShape();
		return true;
	} catch (...) {
		shape.clear();
		return false;
	}
}

bool IsStaticShape(const std::vector<int64_t> &shape)
{
	if (shape.empty())
		return false;
	for (const int64_t dim : shape) {
		if (dim <= 0)
			return false;
	}
	return true;
}

} // namespace

bool InitializeIoBindingSetup(Ort::Session &session, size_t detection_output_index,
			      size_t proto_output_index,
			      const std::vector<std::string> &output_name_storage,
			      IoBindingSetupResult &out, std::string &detail_out)
{
	detail_out.clear();
	out = {};
	try {
		out.io_binding = std::make_unique<Ort::IoBinding>(session);
		/*
		 * Keep Ort::TypeInfo owning wrappers alive while we inspect tensor metadata.
		 * TensorTypeAndShapeInfo is an unowned view in ORT C++ API and can dangle if
		 * chained from temporaries.
		 */
		const auto detection_type_info = session.GetOutputTypeInfo(detection_output_index);
		const auto proto_type_info = session.GetOutputTypeInfo(proto_output_index);
		if (detection_type_info.GetONNXType() != ONNX_TYPE_TENSOR ||
		    proto_type_info.GetONNXType() != ONNX_TYPE_TENSOR) {
			detail_out = "model outputs are not tensors";
			return false;
		}
		const auto detection_info = detection_type_info.GetTensorTypeAndShapeInfo();
		const auto proto_info = proto_type_info.GetTensorTypeAndShapeInfo();
		std::vector<int64_t> detection_shape;
		std::vector<int64_t> proto_shape;
		if (!TryGetTensorShape(detection_info, detection_shape) ||
		    !TryGetTensorShape(proto_info, proto_shape)) {
			detail_out = "output shape metadata unavailable";
			return false;
		}

		if (!IsStaticShape(detection_shape) || !IsStaticShape(proto_shape)) {
			out.io_binding_output_memory_info = std::make_unique<Ort::MemoryInfo>(
				Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
			out.io_binding->BindOutput(output_name_storage[detection_output_index].c_str(),
						   *out.io_binding_output_memory_info);
			out.io_binding->BindOutput(output_name_storage[proto_output_index].c_str(),
						   *out.io_binding_output_memory_info);
			out.io_binding_dynamic_outputs = true;
			out.io_binding_static_outputs = false;
			detail_out = "dynamic outputs bound via CPU memory info";
			return true;
		}

		const size_t detection_elements = detection_info.GetElementCount();
		const size_t proto_elements = proto_info.GetElementCount();
		if (detection_elements == 0 || proto_elements == 0) {
			detail_out = "zero-sized output tensors";
			return false;
		}

		out.bound_detection_shape = std::move(detection_shape);
		out.bound_proto_shape = std::move(proto_shape);
		out.bound_detection_storage.assign(detection_elements, 0.0f);
		out.bound_proto_storage.assign(proto_elements, 0.0f);

		Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		out.bound_detection_output = std::make_unique<Ort::Value>(Ort::Value::CreateTensor<float>(
			mem_info, out.bound_detection_storage.data(), out.bound_detection_storage.size(),
			out.bound_detection_shape.data(), out.bound_detection_shape.size()));
		out.bound_proto_output = std::make_unique<Ort::Value>(Ort::Value::CreateTensor<float>(
			mem_info, out.bound_proto_storage.data(), out.bound_proto_storage.size(),
			out.bound_proto_shape.data(), out.bound_proto_shape.size()));

		out.io_binding->BindOutput(output_name_storage[detection_output_index].c_str(),
					   *out.bound_detection_output);
		out.io_binding->BindOutput(output_name_storage[proto_output_index].c_str(),
					   *out.bound_proto_output);
		out.io_binding_static_outputs = true;
		out.io_binding_dynamic_outputs = false;
		out.io_binding_output_memory_info.reset();
		detail_out = "static bound outputs";
		return true;
	} catch (const Ort::Exception &ex) {
		detail_out = ex.what();
	} catch (...) {
		detail_out = "unknown I/O binding error";
	}
	out = {};
	return false;
}

void ClearIoBindingRuntime(IoBindingRuntimeState &state)
{
	if (state.io_binding)
		state.io_binding->reset();
	if (state.io_binding_static_outputs)
		*state.io_binding_static_outputs = false;
	if (state.io_binding_dynamic_outputs)
		*state.io_binding_dynamic_outputs = false;
	if (state.io_binding_output_memory_info)
		state.io_binding_output_memory_info->reset();
	if (state.bound_detection_shape)
		state.bound_detection_shape->clear();
	if (state.bound_proto_shape)
		state.bound_proto_shape->clear();
	if (state.bound_detection_storage)
		state.bound_detection_storage->clear();
	if (state.bound_proto_storage)
		state.bound_proto_storage->clear();
	if (state.bound_detection_output)
		state.bound_detection_output->reset();
	if (state.bound_proto_output)
		state.bound_proto_output->reset();
}

void ResetSessionRuntimeState(SessionRuntimeState &state)
{
	if (state.mask_dim)
		*state.mask_dim = 0;
	if (state.proto_width)
		*state.proto_width = 0;
	if (state.proto_height)
		*state.proto_height = 0;
	if (state.proto_channel_first)
		*state.proto_channel_first = true;
	if (state.io_binding_enabled)
		*state.io_binding_enabled = false;
	ClearIoBindingRuntime(state.io_binding);
}

bool InitializeIoBindingRuntime(IoBindingRuntimeState &state, std::string &detail_out)
{
	detail_out.clear();
	if (!state.session || !state.output_name_storage || !state.io_binding ||
	    !state.io_binding_static_outputs || !state.io_binding_dynamic_outputs ||
	    !state.io_binding_output_memory_info || !state.bound_detection_shape ||
	    !state.bound_proto_shape || !state.bound_detection_storage || !state.bound_proto_storage ||
	    !state.bound_detection_output || !state.bound_proto_output) {
		detail_out = "invalid I/O binding runtime state";
		ClearIoBindingRuntime(state);
		return false;
	}

	IoBindingSetupResult setup{};
	if (!InitializeIoBindingSetup(*state.session, state.detection_output_index,
				      state.proto_output_index, *state.output_name_storage, setup,
				      detail_out)) {
		ClearIoBindingRuntime(state);
		return false;
	}

	*state.io_binding = std::move(setup.io_binding);
	*state.io_binding_static_outputs = setup.io_binding_static_outputs;
	*state.io_binding_dynamic_outputs = setup.io_binding_dynamic_outputs;
	*state.io_binding_output_memory_info = std::move(setup.io_binding_output_memory_info);
	*state.bound_detection_shape = std::move(setup.bound_detection_shape);
	*state.bound_proto_shape = std::move(setup.bound_proto_shape);
	*state.bound_detection_storage = std::move(setup.bound_detection_storage);
	*state.bound_proto_storage = std::move(setup.bound_proto_storage);
	*state.bound_detection_output = std::move(setup.bound_detection_output);
	*state.bound_proto_output = std::move(setup.bound_proto_output);
	return true;
}

} // namespace lenses::ai::ort::detail

#endif
