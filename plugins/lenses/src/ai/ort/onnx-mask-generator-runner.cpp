#include "ai/ort/onnx-mask-generator-runner.hpp"

#if defined(LENSES_ENABLE_ORT)

#include "ai/ort/onnx-mask-generator-layout.hpp"
#include "lenses/ai/decode/segmentation-decoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

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

} // namespace

std::array<int64_t, 4> BuildOrtInputShape(bool nchw, uint32_t input_width,
					   uint32_t input_height)
{
	if (nchw)
		return {1, 3, (int64_t)input_height, (int64_t)input_width};
	return {1, (int64_t)input_height, (int64_t)input_width, 3};
}

Ort::Value CreateCpuInputTensor(std::vector<float> &input_tensor_values,
				const std::array<int64_t, 4> &input_shape)
{
	const Ort::MemoryInfo mem_info =
		Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	return Ort::Value::CreateTensor<float>(
		mem_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(),
		input_shape.size());
}

bool ExecuteOrtInference(Ort::Session &session, const std::string &selected_input_name,
			 const std::vector<const char *> &output_names,
			 size_t detection_output_index, size_t proto_output_index,
			 const std::vector<std::string> &output_name_storage,
			 bool io_binding_enabled, bool io_binding_static_outputs,
			 bool io_binding_dynamic_outputs, Ort::IoBinding *io_binding,
			 const std::vector<float> &bound_detection_storage,
			 const std::vector<float> &bound_proto_storage,
			 const std::vector<int64_t> &bound_detection_shape,
			 const std::vector<int64_t> &bound_proto_shape, Ort::Value &input_tensor,
			 OrtInferenceOutputs &out)
{
	out = {};
	if (io_binding_enabled && io_binding &&
	    (io_binding_static_outputs || io_binding_dynamic_outputs)) {
		io_binding->ClearBoundInputs();
		io_binding->BindInput(selected_input_name.c_str(), input_tensor);
		session.Run(Ort::RunOptions{nullptr}, *io_binding);
		io_binding->SynchronizeOutputs();

		if (io_binding_static_outputs) {
			out.detection_data = bound_detection_storage.data();
			out.proto_data = bound_proto_storage.data();
			out.detection_elements = bound_detection_storage.size();
			out.proto_elements = bound_proto_storage.size();
			out.detection_shape = bound_detection_shape;
			out.proto_shape = bound_proto_shape;
			out.has_detection_shape = !out.detection_shape.empty();
			out.has_proto_shape = !out.proto_shape.empty();
			return true;
		}

		out.owned_io_bound_outputs = io_binding->GetOutputValues();
		out.io_bound_output_names = io_binding->GetOutputNames();
		if (out.owned_io_bound_outputs.empty())
			return false;

		size_t detection_output_pos = std::numeric_limits<size_t>::max();
		size_t proto_output_pos = std::numeric_limits<size_t>::max();
		if (out.owned_io_bound_outputs.size() == out.io_bound_output_names.size()) {
			const std::string detection_name = output_name_storage[detection_output_index];
			const std::string proto_name = output_name_storage[proto_output_index];
			for (size_t i = 0; i < out.io_bound_output_names.size(); ++i) {
				if (detection_output_pos == std::numeric_limits<size_t>::max() &&
				    out.io_bound_output_names[i] == detection_name)
					detection_output_pos = i;
				if (proto_output_pos == std::numeric_limits<size_t>::max() &&
				    out.io_bound_output_names[i] == proto_name)
					proto_output_pos = i;
			}
		}
		if (detection_output_pos == std::numeric_limits<size_t>::max() ||
		    proto_output_pos == std::numeric_limits<size_t>::max()) {
			if (out.owned_io_bound_outputs.size() <=
			    std::max(detection_output_index, proto_output_index))
				return false;
			detection_output_pos = detection_output_index;
			proto_output_pos = proto_output_index;
		}

		const auto detection_info =
			out.owned_io_bound_outputs[detection_output_pos].GetTensorTypeAndShapeInfo();
		const auto proto_info =
			out.owned_io_bound_outputs[proto_output_pos].GetTensorTypeAndShapeInfo();
		out.detection_elements = detection_info.GetElementCount();
		out.proto_elements = proto_info.GetElementCount();
		out.has_detection_shape = TryGetTensorShape(detection_info, out.detection_shape);
		out.has_proto_shape = TryGetTensorShape(proto_info, out.proto_shape);
		out.detection_data =
			out.owned_io_bound_outputs[detection_output_pos].GetTensorData<float>();
		out.proto_data = out.owned_io_bound_outputs[proto_output_pos].GetTensorData<float>();
		return true;
	}

	const char *selected_input = selected_input_name.c_str();
	out.owned_run_outputs = session.Run(Ort::RunOptions{nullptr}, &selected_input, &input_tensor, 1,
					    output_names.data(), output_names.size());
	if (out.owned_run_outputs.size() <= std::max(detection_output_index, proto_output_index))
		return false;

	const auto detection_info =
		out.owned_run_outputs[detection_output_index].GetTensorTypeAndShapeInfo();
	const auto proto_info =
		out.owned_run_outputs[proto_output_index].GetTensorTypeAndShapeInfo();
	out.detection_elements = detection_info.GetElementCount();
	out.proto_elements = proto_info.GetElementCount();
	out.has_detection_shape = TryGetTensorShape(detection_info, out.detection_shape);
	out.has_proto_shape = TryGetTensorShape(proto_info, out.proto_shape);
	out.detection_data = out.owned_run_outputs[detection_output_index].GetTensorData<float>();
	out.proto_data = out.owned_run_outputs[proto_output_index].GetTensorData<float>();
	return true;
}

bool DecodeOrtOutputsToMaskFrame(const OrtInferenceOutputs &ort_outputs,
				 const lenses::core::FrameTicket &frame, uint32_t input_width,
				 uint32_t input_height, const DecodeLimits &limits,
				 uint32_t &mask_dim_out, uint32_t &proto_width_out,
				 uint32_t &proto_height_out, bool &proto_channel_first_out,
				 lenses::core::MaskFrame &out_frame, double &decode_ms_out)
{
	if (!ort_outputs.detection_data || !ort_outputs.proto_data || ort_outputs.detection_elements == 0 ||
	    ort_outputs.proto_elements == 0)
		return false;

	// Prefer channel-first [N,mask_dim,H,W]. Fall back to [N,H,W,mask_dim].
	bool runtime_proto_channel_first = true;
	uint32_t runtime_mask_dim = 0;
	uint32_t runtime_proto_width = 0;
	uint32_t runtime_proto_height = 0;
	bool parsed_proto_layout = false;
	if (ort_outputs.has_proto_shape && ort_outputs.proto_shape.size() == 4) {
		if (ort_outputs.proto_shape[1] > 0 && ort_outputs.proto_shape[2] > 0 &&
		    ort_outputs.proto_shape[3] > 0 && ort_outputs.proto_shape[1] <= 512) {
			runtime_proto_channel_first = true;
			runtime_mask_dim = (uint32_t)ort_outputs.proto_shape[1];
			runtime_proto_height = (uint32_t)ort_outputs.proto_shape[2];
			runtime_proto_width = (uint32_t)ort_outputs.proto_shape[3];
			parsed_proto_layout = true;
		} else if (ort_outputs.proto_shape[1] > 0 && ort_outputs.proto_shape[2] > 0 &&
			   ort_outputs.proto_shape[3] > 0) {
			runtime_proto_channel_first = false;
			runtime_proto_height = (uint32_t)ort_outputs.proto_shape[1];
			runtime_proto_width = (uint32_t)ort_outputs.proto_shape[2];
			runtime_mask_dim = (uint32_t)ort_outputs.proto_shape[3];
			parsed_proto_layout = true;
		}
	}
	if (!parsed_proto_layout) {
		if (!InferProtoLayoutFromElementCount(
			    ort_outputs.proto_elements, input_width, input_height,
			    limits.max_detection_features, runtime_mask_dim, runtime_proto_width,
			    runtime_proto_height, runtime_proto_channel_first))
			return false;
	}

	if (runtime_proto_width > limits.max_proto_dimension ||
	    runtime_proto_height > limits.max_proto_dimension ||
	    runtime_mask_dim > limits.max_detection_features)
		return false;
	if (runtime_mask_dim == 0 || runtime_proto_width == 0 || runtime_proto_height == 0)
		return false;

	DetectionLayout detection_layout{};
	bool parsed_detection_layout = false;
	if (ort_outputs.has_detection_shape)
		parsed_detection_layout = BuildDetectionLayout(
			ort_outputs.detection_shape, runtime_mask_dim, limits.max_detection_candidates,
			limits.max_detection_features, detection_layout);
	if (!parsed_detection_layout)
		parsed_detection_layout = InferDetectionLayoutFromElementCount(
			ort_outputs.detection_elements, runtime_mask_dim,
			limits.default_coco_class_count, limits.max_detection_features,
			limits.max_detection_candidates, detection_layout);
	if (!parsed_detection_layout)
		return false;

	const size_t detection_required =
		detection_layout.candidate_count * detection_layout.feature_count;
	if (ort_outputs.detection_elements < detection_required)
		return false;

	const auto decode_started = std::chrono::steady_clock::now();
	std::vector<lenses::ai::decode::SegmentationCandidate> candidates;
	DecodeDetections(ort_outputs.detection_data, detection_layout, runtime_mask_dim, input_width,
			 input_height, limits.confidence_threshold, limits.nms_iou_threshold,
			 limits.max_detections, candidates);

	lenses::ai::decode::SegmentationTensors tensors{};
	tensors.mask_dim = runtime_mask_dim;
	tensors.proto_width = runtime_proto_width;
	tensors.proto_height = runtime_proto_height;
	tensors.candidates = std::move(candidates);
	tensors.prototypes.assign((size_t)runtime_mask_dim * runtime_proto_width *
					  runtime_proto_height,
				  0.0f);

	const size_t plane_area = (size_t)runtime_proto_width * runtime_proto_height;
	const size_t proto_required = (size_t)runtime_mask_dim * plane_area;
	if (ort_outputs.proto_elements < proto_required)
		return false;

	if (runtime_proto_channel_first) {
		std::copy(ort_outputs.proto_data, ort_outputs.proto_data + proto_required,
			  tensors.prototypes.begin());
	} else {
		for (uint32_t y = 0; y < runtime_proto_height; ++y) {
			for (uint32_t x = 0; x < runtime_proto_width; ++x) {
				for (uint32_t c = 0; c < runtime_mask_dim; ++c) {
					const size_t src_index =
						((size_t)y * runtime_proto_width + x) * runtime_mask_dim + c;
					const size_t dst_index =
						(size_t)c * plane_area +
						((size_t)y * runtime_proto_width + x);
					tensors.prototypes[dst_index] = ort_outputs.proto_data[src_index];
				}
			}
		}
	}

	lenses::ai::decode::SegmentationDecodeConfig decode_config{};
	decode_config.source_width = frame.source_width;
	decode_config.source_height = frame.source_height;
	decode_config.confidence_threshold = limits.confidence_threshold;
	decode_config.mask_threshold = limits.mask_threshold;
	decode_config.min_area_ratio = 0.0f;
	out_frame = lenses::ai::decode::DecodeSegmentationMasks(tensors, decode_config, frame.frame_id,
								frame.timestamp_ns);
	decode_ms_out = std::chrono::duration<double, std::milli>(
			    std::chrono::steady_clock::now() - decode_started)
			    .count();

	mask_dim_out = runtime_mask_dim;
	proto_width_out = runtime_proto_width;
	proto_height_out = runtime_proto_height;
	proto_channel_first_out = runtime_proto_channel_first;
	return true;
}

} // namespace lenses::ai::ort::detail

#endif
