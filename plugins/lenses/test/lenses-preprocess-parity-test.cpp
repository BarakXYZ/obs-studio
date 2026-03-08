#include "ai/ort/onnx-mask-generator-preprocess.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const std::string &message)
{
	if (condition)
		return true;
	std::cerr << "FAIL: " << message << std::endl;
	return false;
}

bool NearlyEqual(float a, float b, float eps = 1.0f / 255.0f)
{
	return std::fabs(a - b) <= eps;
}

lenses::core::FrameTicket MakeSinglePixelFrame(uint8_t b, uint8_t g, uint8_t r, uint8_t a)
{
	lenses::core::FrameTicket frame{};
	frame.frame_id = 1;
	frame.source_width = 1;
	frame.source_height = 1;
	frame.timestamp_ns = 1;
	frame.image_width = 1;
	frame.image_height = 1;
	frame.image_linesize = 4;
	frame.image_bgra = {b, g, r, a};
	return frame;
}

bool VerifyNormalizedRgbTensor(const std::vector<float> &tensor, float expected_r, float expected_g,
			       float expected_b)
{
	if (!Expect(tensor.size() == 3, "tensor must have exactly 3 values"))
		return false;
	for (size_t i = 0; i < tensor.size(); ++i) {
		if (!Expect(tensor[i] >= 0.0f && tensor[i] <= 1.0f,
			    "tensor values must be normalized to [0,1]")) {
			return false;
		}
	}
	if (!NearlyEqual(tensor[0], expected_r)) {
		std::cerr << "FAIL: R channel mismatch expected=" << expected_r
			  << " actual=" << tensor[0] << std::endl;
		return false;
	}
	if (!NearlyEqual(tensor[1], expected_g)) {
		std::cerr << "FAIL: G channel mismatch expected=" << expected_g
			  << " actual=" << tensor[1] << std::endl;
		return false;
	}
	if (!NearlyEqual(tensor[2], expected_b)) {
		std::cerr << "FAIL: B channel mismatch expected=" << expected_b
			  << " actual=" << tensor[2] << std::endl;
		return false;
	}
	return true;
}

bool TestPrepareOrtInputAutoIsNormalized()
{
	const auto frame = MakeSinglePixelFrame(255, 128, 64, 0);
	const float expected_r = 64.0f / 255.0f;
	const float expected_g = 128.0f / 255.0f;
	const float expected_b = 1.0f;

	lenses::core::RuntimeConfig config{};
	config.preprocess_mode = lenses::core::PreprocessMode::Auto;

	std::vector<float> input_tensor_values;
	std::vector<uint8_t> resized_bgra;
	std::vector<uint8_t> plane_r;
	std::vector<uint8_t> plane_g;
	std::vector<uint8_t> plane_b;
	std::vector<uint8_t> plane_a;
	std::vector<float> plane_r_f;
	std::vector<float> plane_g_f;
	std::vector<float> plane_b_f;
	double preprocess_ms = 0.0;

	const bool ok = lenses::ai::ort::detail::PrepareOrtInputTensorValues(
		frame, config, 1, 1, true, input_tensor_values, resized_bgra, plane_r, plane_g, plane_b,
		plane_a, plane_r_f, plane_g_f, plane_b_f, preprocess_ms);
	if (!Expect(ok, "PrepareOrtInputTensorValues(auto) should succeed"))
		return false;
	return VerifyNormalizedRgbTensor(input_tensor_values, expected_r, expected_g, expected_b);
}

#if defined(__APPLE__)
bool TestAcceleratePathIsNormalized()
{
	const auto frame = MakeSinglePixelFrame(255, 128, 64, 0);
	const float expected_r = 64.0f / 255.0f;
	const float expected_g = 128.0f / 255.0f;
	const float expected_b = 1.0f;

	std::vector<float> dst;
	std::vector<uint8_t> resized_bgra;
	std::vector<uint8_t> plane_r;
	std::vector<uint8_t> plane_g;
	std::vector<uint8_t> plane_b;
	std::vector<uint8_t> plane_a;
	std::vector<float> plane_r_f;
	std::vector<float> plane_g_f;
	std::vector<float> plane_b_f;

	const bool ok = lenses::ai::ort::detail::ResizeBGRAtoRGBFloatAccelerate(
		frame, 1, 1, true, dst, resized_bgra, plane_r, plane_g, plane_b, plane_a, plane_r_f,
		plane_g_f, plane_b_f);
	if (!Expect(ok, "ResizeBGRAtoRGBFloatAccelerate should succeed"))
		return false;
	return VerifyNormalizedRgbTensor(dst, expected_r, expected_g, expected_b);
}
#endif

} // namespace

int main()
{
	bool ok = true;
	ok = TestPrepareOrtInputAutoIsNormalized() && ok;
#if defined(__APPLE__)
	ok = TestAcceleratePathIsNormalized() && ok;
#endif
	if (!ok)
		return 1;

	std::cout << "lenses-preprocess-parity-test: PASS" << std::endl;
	return 0;
}
