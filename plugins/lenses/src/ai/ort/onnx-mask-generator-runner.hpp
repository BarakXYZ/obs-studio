#pragma once

#include "lenses/core/interfaces.hpp"

#if defined(LENSES_ENABLE_ORT)

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lenses::ai::ort::detail {

struct OrtInferenceOutputs {
	const float *detection_data = nullptr;
	const float *proto_data = nullptr;
	size_t detection_elements = 0;
	size_t proto_elements = 0;
	std::vector<int64_t> detection_shape;
	std::vector<int64_t> proto_shape;
	bool has_detection_shape = false;
	bool has_proto_shape = false;
	std::vector<Ort::Value> owned_run_outputs;
	std::vector<Ort::Value> owned_io_bound_outputs;
	std::vector<std::string> io_bound_output_names;
};

struct DecodeLimits {
	uint32_t max_detection_features = 0;
	uint32_t max_detection_candidates = 0;
	uint32_t max_proto_dimension = 0;
	uint32_t default_coco_class_count = 0;
	uint32_t max_detections = 0;
	float confidence_threshold = 0.0f;
	float nms_iou_threshold = 0.0f;
	float mask_threshold = 0.0f;
};

std::array<int64_t, 4> BuildOrtInputShape(bool nchw, uint32_t input_width,
					   uint32_t input_height);

Ort::Value CreateCpuInputTensor(std::vector<float> &input_tensor_values,
				const std::array<int64_t, 4> &input_shape);

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
			 OrtInferenceOutputs &out);

bool DecodeOrtOutputsToMaskFrame(const OrtInferenceOutputs &ort_outputs,
				 const lenses::core::FrameTicket &frame, uint32_t input_width,
				 uint32_t input_height, const DecodeLimits &limits,
				 uint32_t &mask_dim_out, uint32_t &proto_width_out,
				 uint32_t &proto_height_out, bool &proto_channel_first_out,
				 lenses::core::MaskFrame &out_frame, double &decode_ms_out);

} // namespace lenses::ai::ort::detail

#endif
