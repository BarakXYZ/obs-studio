#pragma once

#include "lenses/core/interfaces.hpp"

#include <cstdint>
#include <vector>

namespace lenses::ai::ort::detail {

float Clamp01(float value);

double PercentileFromSamples(const std::vector<double> &samples, double percentile);

float BoxIoU(const lenses::core::NormalizedRect &a, const lenses::core::NormalizedRect &b);

void BuildLumaSample(const lenses::core::FrameTicket &frame, std::vector<uint8_t> &sample);

bool IsFrameSimilar(const lenses::core::FrameTicket &frame, std::vector<uint8_t> &previous_sample,
		   std::vector<uint8_t> &scratch_sample, float threshold_norm,
		   float *out_mad_norm);

uint32_t SimilaritySkipBudget(uint32_t target_fps);

void ResizeBGRAtoRGBFloatScalar(const lenses::core::FrameTicket &frame, uint32_t dst_width,
			      uint32_t dst_height, bool nchw, std::vector<float> &dst);

#if defined(__APPLE__)
bool ResizeBGRAtoRGBFloatAccelerate(const lenses::core::FrameTicket &frame, uint32_t dst_width,
				  uint32_t dst_height, bool nchw,
				  std::vector<float> &dst,
				  std::vector<uint8_t> &resized_bgra,
				  std::vector<uint8_t> &plane_r,
				  std::vector<uint8_t> &plane_g,
				  std::vector<uint8_t> &plane_b,
				  std::vector<uint8_t> &plane_a,
				  std::vector<float> &plane_r_f,
				  std::vector<float> &plane_g_f,
				  std::vector<float> &plane_b_f);
#endif

bool PrepareOrtInputTensorValues(const lenses::core::FrameTicket &frame,
				 const lenses::core::RuntimeConfig &config,
				 uint32_t input_width, uint32_t input_height, bool nchw,
				 std::vector<float> &input_tensor_values,
				 std::vector<uint8_t> &resized_bgra,
				 std::vector<uint8_t> &plane_r,
				 std::vector<uint8_t> &plane_g,
				 std::vector<uint8_t> &plane_b,
				 std::vector<uint8_t> &plane_a,
				 std::vector<float> &plane_r_f,
				 std::vector<float> &plane_g_f,
				 std::vector<float> &plane_b_f,
				 double &preprocess_ms_out);

bool TryParseCoreMLCapabilityLog(const char *message, uint32_t &out_partitions,
			       uint32_t &out_total_nodes,
			       uint32_t &out_supported_nodes);

} // namespace lenses::ai::ort::detail
