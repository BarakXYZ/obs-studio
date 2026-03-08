#include "ai/ort/onnx-mask-generator-session-io.hpp"

#if defined(LENSES_ENABLE_ORT)

#include "ai/ort/onnx-mask-generator-bootstrap.hpp"
#include "ai/ort/onnx-mask-generator-provider.hpp"

#include <obs-module.h>

#include <algorithm>
#include <string>
#include <vector>

namespace lenses::ai::ort::detail {

namespace {

bool TryInferStaticInputDimensions(const std::vector<int64_t> &shape, uint32_t *out_width,
				   uint32_t *out_height)
{
	if (!out_width || !out_height || shape.size() < 4)
		return false;

	/* NCHW: [N,3,H,W] */
	if (shape[1] == 3 && shape[2] > 0 && shape[3] > 0) {
		*out_width = (uint32_t)shape[3];
		*out_height = (uint32_t)shape[2];
		return true;
	}
	/* NHWC: [N,H,W,3] */
	if (shape[3] == 3 && shape[1] > 0 && shape[2] > 0) {
		*out_width = (uint32_t)shape[2];
		*out_height = (uint32_t)shape[1];
		return true;
	}

	return false;
}

std::string ShapeToString(const std::vector<int64_t> &shape)
{
	std::string out = "[";
	for (size_t i = 0; i < shape.size(); ++i) {
		if (i > 0)
			out += ",";
		out += std::to_string(shape[i]);
	}
	out += "]";
	return out;
}

} // namespace

void BuildNameViews(const std::vector<std::string> &storage, std::vector<const char *> &views)
{
	views.clear();
	views.reserve(storage.size());
	for (const auto &name : storage)
		views.push_back(name.c_str());
}

bool InitializeSessionModelIo(Ort::Session &session, const lenses::core::RuntimeConfig &config,
			      SessionModelIoInfo &out, std::string &error_out)
{
	auto fail = [&error_out](const std::string &reason) {
		error_out = reason;
		return false;
	};

	Ort::AllocatorWithDefaultOptions allocator;
	out = {};

	const size_t input_count = session.GetInputCount();
	if (input_count == 0)
		return fail("model has zero inputs");

	out.input_name_storage.clear();
	out.input_name_storage.reserve(input_count);
	for (size_t i = 0; i < input_count; ++i) {
		auto name = session.GetInputNameAllocated(i, allocator);
		out.input_name_storage.emplace_back(name.get());
	}

	const size_t output_count = session.GetOutputCount();
	if (output_count < 2)
		return fail("model has fewer than 2 outputs");

	out.output_name_storage.clear();
	out.output_name_storage.reserve(output_count);
	for (size_t i = 0; i < output_count; ++i) {
		auto name = session.GetOutputNameAllocated(i, allocator);
		out.output_name_storage.emplace_back(name.get());
	}

	bool found_tensor_input = false;
	for (size_t i = 0; i < input_count; ++i) {
		const auto input_type = session.GetInputTypeInfo(i);
		if (input_type.GetONNXType() != ONNX_TYPE_TENSOR)
			continue;
		out.selected_input_name = out.input_name_storage[i];
		found_tensor_input = true;
		break;
	}
	if (!found_tensor_input)
		return fail("failed to find a supported tensor model input");

	// Prefer semantic output names, then positional [0,1] as a compatibility path.
	for (size_t i = 0; i < output_count; ++i) {
		const std::string &name = out.output_name_storage[i];
		if (out.detection_output_index == std::numeric_limits<size_t>::max() &&
		    (name.find("output0") != std::string::npos ||
		     name.find("detect") != std::string::npos ||
		     name.find("boxes") != std::string::npos)) {
			out.detection_output_index = i;
		}
		if (out.proto_output_index == std::numeric_limits<size_t>::max() &&
		    (name.find("output1") != std::string::npos ||
		     name.find("proto") != std::string::npos ||
		     name.find("mask") != std::string::npos)) {
			out.proto_output_index = i;
		}
	}

	if ((out.detection_output_index == std::numeric_limits<size_t>::max() ||
	     out.proto_output_index == std::numeric_limits<size_t>::max()) &&
	    output_count >= 2) {
		out.detection_output_index = 0;
		out.proto_output_index = 1;
	}

	if (out.detection_output_index == std::numeric_limits<size_t>::max() ||
	    out.proto_output_index == std::numeric_limits<size_t>::max()) {
		std::string output_debug;
		for (size_t i = 0; i < output_count; ++i) {
			if (!output_debug.empty())
				output_debug += " ; ";
			output_debug += "i=" + std::to_string(i) + " name=" + out.output_name_storage[i];
		}
		blog(LOG_WARNING,
		     "[lenses] Failed to classify ORT outputs for model='%s'. outputs=[%s]",
		     config.model_path.c_str(), output_debug.c_str());
		return fail("failed to identify detection/prototype outputs");
	}

	error_out.clear();
	return true;
}

bool LoadSessionModelIoRuntime(Ort::Session &session, const lenses::core::RuntimeConfig &config,
			       SessionRuntimeIoState &state, uint32_t max_input_dimension,
			       std::string &error_out)
{
	auto fail = [&error_out](const std::string &reason) {
		error_out = reason;
		return false;
	};

	if (!state.input_name_storage || !state.output_name_storage || !state.selected_input_name ||
	    !state.detection_output_index || !state.proto_output_index || !state.input_names ||
	    !state.output_names || !state.input_width || !state.input_height) {
		return fail("invalid runtime session I/O state");
	}

	SessionModelIoInfo session_io{};
	if (!InitializeSessionModelIo(session, config, session_io, error_out))
		return false;

	*state.input_name_storage = std::move(session_io.input_name_storage);
	*state.output_name_storage = std::move(session_io.output_name_storage);
	*state.selected_input_name = std::move(session_io.selected_input_name);
	*state.detection_output_index = session_io.detection_output_index;
	*state.proto_output_index = session_io.proto_output_index;
	BuildNameViews(*state.input_name_storage, *state.input_names);
	BuildNameViews(*state.output_name_storage, *state.output_names);

	*state.input_width = std::max<uint32_t>(1, config.input_width);
	*state.input_height = std::max<uint32_t>(1, config.input_height);

	size_t selected_input_index = 0;
	bool found_selected_input_index = false;
	for (size_t i = 0; i < state.input_name_storage->size(); ++i) {
		if ((*state.input_name_storage)[i] == *state.selected_input_name) {
			selected_input_index = i;
			found_selected_input_index = true;
			break;
		}
	}
	if (!found_selected_input_index)
		return fail("selected ORT input name is missing from runtime input list");

	try {
		auto input_type_info = session.GetInputTypeInfo(selected_input_index);
		if (input_type_info.GetONNXType() == ONNX_TYPE_TENSOR) {
			auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
			const std::vector<int64_t> input_shape = tensor_info.GetShape();
			uint32_t model_input_width = 0;
			uint32_t model_input_height = 0;
				if (TryInferStaticInputDimensions(input_shape, &model_input_width,
								 &model_input_height)) {
					if (*state.input_width != model_input_width ||
					    *state.input_height != model_input_height) {
						const std::string shape_text = ShapeToString(input_shape);
						/*
						 * Keep runtime deterministic without hard-failing when metadata/profile
						 * settings lag behind model truth. ORT reports static input geometry;
						 * we clamp to model dims and continue.
						 */
						blog(LOG_WARNING,
						     "[lenses] configured input %ux%u is incompatible with model static input %ux%u (shape=%s); forcing model input dimensions%s",
						     *state.input_width, *state.input_height, model_input_width,
						     model_input_height, shape_text.c_str(),
						     config.strict_runtime_checks
							     ? " [strict-runtime auto-correct]"
							     : "");
						*state.input_width = model_input_width;
						*state.input_height = model_input_height;
					}
				}
		}
	} catch (const Ort::Exception &ex) {
		return fail(std::string("failed to read ORT input tensor shape: ") + ex.what());
	}
	blog(LOG_INFO, "[lenses] ORT input dimensions set to %ux%u (configured=%ux%u)",
	     *state.input_width, *state.input_height, config.input_width, config.input_height);

	if (*state.input_width == 0 || *state.input_height == 0)
		return fail("input tensor shape has zero width/height");
	if (*state.input_width > max_input_dimension || *state.input_height > max_input_dimension)
		return fail("input tensor dimensions exceed safety limits");

	error_out.clear();
	return true;
}

bool InitializeOrtSessionRuntime(SessionOrchestrationContext &context,
				 std::string &error_out)
{
	if (!context.config || !context.env || !context.session ||
	    !context.active_execution_provider || !context.coreml_requested ||
	    !context.coreml_enabled || !context.cpu_ep_fallback_disabled) {
		error_out = "session orchestration context is incomplete";
		return false;
	}

	if (context.config->model_path.empty()) {
		error_out = "model path is empty";
		return false;
	}

	CreateOrtSessionContext create_context{};
	create_context.config = context.config;
	create_context.env = context.env;
	create_context.session = context.session;
	create_context.log_callback = context.log_callback;
	create_context.log_callback_user_data = context.log_callback_user_data;

	CreateOrtSessionResult create_result{};
	if (!CreateOrtSession(create_context, create_result, error_out)) {
		*context.active_execution_provider = std::move(create_result.active_execution_provider);
		*context.coreml_requested = create_result.coreml_requested;
		*context.coreml_enabled = create_result.coreml_enabled;
		*context.cpu_ep_fallback_disabled = create_result.cpu_ep_fallback_disabled;
		return false;
	}

	*context.active_execution_provider = std::move(create_result.active_execution_provider);
	*context.coreml_requested = create_result.coreml_requested;
	*context.coreml_enabled = create_result.coreml_enabled;
	*context.cpu_ep_fallback_disabled = create_result.cpu_ep_fallback_disabled;

	if (!LoadSessionModelIoRuntime(*context.session->get(), *context.config,
				       context.session_runtime_io_state,
				       context.max_input_dimension, error_out)) {
		return false;
	}
	context.io_binding_runtime_state.session = context.session->get();
	context.io_binding_runtime_state.detection_output_index =
		*context.session_runtime_io_state.detection_output_index;
	context.io_binding_runtime_state.proto_output_index =
		*context.session_runtime_io_state.proto_output_index;
	ResetSessionRuntimeState(context.session_runtime_state);

	LogSessionOptionsSummary(*context.config, create_result.configured_intra_spinning,
				 create_result.configured_inter_spinning,
				 create_result.cpu_ep_fallback_disabled);

	if (context.config->enable_iobinding) {
		std::string io_binding_detail;
		if (InitializeIoBindingRuntime(context.io_binding_runtime_state,
					       io_binding_detail)) {
			*context.session_runtime_state.io_binding_enabled = true;
			blog(LOG_INFO, "[lenses] ORT I/O binding enabled (%s)",
			     io_binding_detail.c_str());
		} else {
			blog(LOG_INFO, "[lenses] ORT I/O binding disabled (%s)",
			     io_binding_detail.empty()
				     ? "not available for current model layout"
				     : io_binding_detail.c_str());
		}
	}

	error_out.clear();
	return true;
}

} // namespace lenses::ai::ort::detail

#endif
