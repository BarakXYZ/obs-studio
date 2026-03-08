#include "ai/ort/onnx-mask-generator-preprocess.hpp"

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace lenses::ai::ort::detail {
namespace {

constexpr size_t kSimilaritySampleMaxPixels = 4096;
constexpr uint32_t kSimilarityMinUpdateFps = 20U;
constexpr uint32_t kSimilarityMaxConsecutiveSkips = 2U;

constexpr const char *kCoreMLCapabilityPartitionsLabel =
	"number of partitions supported by CoreML:";
constexpr const char *kCoreMLCapabilityTotalNodesLabel =
	"number of nodes in the graph:";
constexpr const char *kCoreMLCapabilitySupportedNodesLabel =
	"number of nodes supported by CoreML:";

bool TryParseUintAfterLabel(const char *message, const char *label, uint32_t &out_value)
{
	if (!message || !label)
		return false;

	const char *pos = strstr(message, label);
	if (!pos)
		return false;
	pos += strlen(label);
	while (*pos && std::isspace((unsigned char)*pos))
		++pos;

	char *end = nullptr;
	const unsigned long long parsed = strtoull(pos, &end, 10);
	if (end == pos)
		return false;
	if (parsed > std::numeric_limits<uint32_t>::max())
		return false;

	out_value = (uint32_t)parsed;
	return true;
}

} // namespace

float Clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

double PercentileFromSamples(const std::vector<double> &samples, double percentile)
{
	if (samples.empty())
		return 0.0;
	if (percentile <= 0.0)
		return samples.front();
	if (percentile >= 100.0)
		return samples.back();

	const double rank = (percentile / 100.0) * (double)(samples.size() - 1U);
	const size_t low_index = (size_t)std::floor(rank);
	const size_t high_index = (size_t)std::ceil(rank);
	const double weight = rank - (double)low_index;
	if (low_index == high_index)
		return samples[low_index];
	return samples[low_index] * (1.0 - weight) + samples[high_index] * weight;
}

float BoxIoU(const lenses::core::NormalizedRect &a, const lenses::core::NormalizedRect &b)
{
	const float ax2 = a.x + a.width;
	const float ay2 = a.y + a.height;
	const float bx2 = b.x + b.width;
	const float by2 = b.y + b.height;

	const float ix1 = std::max(a.x, b.x);
	const float iy1 = std::max(a.y, b.y);
	const float ix2 = std::min(ax2, bx2);
	const float iy2 = std::min(ay2, by2);

	const float iw = std::max(0.0f, ix2 - ix1);
	const float ih = std::max(0.0f, iy2 - iy1);
	const float inter = iw * ih;
	if (inter <= 0.0f)
		return 0.0f;

	const float union_area = a.width * a.height + b.width * b.height - inter;
	if (union_area <= 0.0f)
		return 0.0f;
	return inter / union_area;
}

void BuildLumaSample(const lenses::core::FrameTicket &frame, std::vector<uint8_t> &sample)
{
	sample.clear();
	if (frame.image_bgra.empty() || frame.image_width == 0 || frame.image_height == 0 ||
	    frame.image_linesize < frame.image_width * 4U) {
		return;
	}

	const uint32_t width = frame.image_width;
	const uint32_t height = frame.image_height;
	const uint32_t base_step = (uint32_t)std::max<size_t>(
		1, (size_t)std::sqrt(((size_t)width * (size_t)height) / kSimilaritySampleMaxPixels));

	for (uint32_t y = 0; y < height; y += base_step) {
		const uint8_t *row = frame.image_bgra.data() + (size_t)y * frame.image_linesize;
		for (uint32_t x = 0; x < width; x += base_step) {
			const uint8_t b = row[(size_t)x * 4U + 0U];
			const uint8_t g = row[(size_t)x * 4U + 1U];
			const uint8_t r = row[(size_t)x * 4U + 2U];
			const uint8_t y8 = (uint8_t)((54U * r + 183U * g + 19U * b) >> 8);
			sample.push_back(y8);
		}
	}
}

bool IsFrameSimilar(const lenses::core::FrameTicket &frame, std::vector<uint8_t> &previous_sample,
		   std::vector<uint8_t> &scratch_sample, float threshold_norm,
		   float *out_mad_norm)
{
	if (out_mad_norm)
		*out_mad_norm = 1.0f;
	BuildLumaSample(frame, scratch_sample);
	if (scratch_sample.empty())
		return false;
	if (previous_sample.empty() || previous_sample.size() != scratch_sample.size()) {
		previous_sample = scratch_sample;
		return false;
	}

	uint64_t total_abs = 0;
	for (size_t i = 0; i < scratch_sample.size(); ++i)
		total_abs += (uint64_t)std::abs((int)scratch_sample[i] - (int)previous_sample[i]);
	const float mad = (float)total_abs / (255.0f * (float)scratch_sample.size());
	if (out_mad_norm)
		*out_mad_norm = mad;
	previous_sample = scratch_sample;
	return mad < threshold_norm;
}

uint32_t SimilaritySkipBudget(uint32_t target_fps)
{
	const uint32_t safe_target = std::max<uint32_t>(1U, target_fps);
	if (safe_target <= kSimilarityMinUpdateFps)
		return 0U;

	const uint32_t ratio = safe_target / kSimilarityMinUpdateFps;
	if (ratio <= 1U)
		return 0U;

	const uint32_t budget = ratio - 1U;
	return std::min<uint32_t>(kSimilarityMaxConsecutiveSkips, budget);
}

void ResizeBGRAtoRGBFloatScalar(const lenses::core::FrameTicket &frame, uint32_t dst_width,
			      uint32_t dst_height, bool nchw, std::vector<float> &dst)
{
	if (dst_width == 0 || dst_height == 0 || frame.image_bgra.empty() || frame.image_linesize == 0) {
		dst.clear();
		return;
	}

	const uint32_t src_width = frame.image_width;
	const uint32_t src_height = frame.image_height;
	if (src_width == 0 || src_height == 0) {
		dst.clear();
		return;
	}

	const size_t pixel_count = (size_t)dst_width * (size_t)dst_height;
	dst.assign(pixel_count * 3U, 0.0f);

	for (uint32_t y = 0; y < dst_height; ++y) {
		const float src_yf = ((float)y + 0.5f) * ((float)src_height / (float)dst_height) - 0.5f;
		const uint32_t sy = (uint32_t)std::clamp((int)std::lround(src_yf), 0, (int)src_height - 1);
		for (uint32_t x = 0; x < dst_width; ++x) {
			const float src_xf =
				((float)x + 0.5f) * ((float)src_width / (float)dst_width) - 0.5f;
			const uint32_t sx = (uint32_t)std::clamp((int)std::lround(src_xf), 0,
							 (int)src_width - 1);
			const size_t src_index = (size_t)sy * frame.image_linesize + (size_t)sx * 4U;
			if (src_index + 2U >= frame.image_bgra.size())
				continue;

			const float b = (float)frame.image_bgra[src_index + 0U] / 255.0f;
			const float g = (float)frame.image_bgra[src_index + 1U] / 255.0f;
			const float r = (float)frame.image_bgra[src_index + 2U] / 255.0f;

			if (nchw) {
				const size_t index = (size_t)y * dst_width + x;
				dst[index + 0U * pixel_count] = r;
				dst[index + 1U * pixel_count] = g;
				dst[index + 2U * pixel_count] = b;
			} else {
				const size_t index = ((size_t)y * dst_width + x) * 3U;
				dst[index + 0U] = r;
				dst[index + 1U] = g;
				dst[index + 2U] = b;
			}
		}
	}
}

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
				  std::vector<float> &plane_b_f)
{
	if (dst_width == 0 || dst_height == 0 || frame.image_bgra.empty() || frame.image_linesize == 0)
		return false;
	if (frame.image_width == 0 || frame.image_height == 0)
		return false;

	const size_t pixel_count = (size_t)dst_width * (size_t)dst_height;
	const size_t bgra_size = pixel_count * 4U;
	resized_bgra.resize(bgra_size);
	plane_r.resize(pixel_count);
	plane_g.resize(pixel_count);
	plane_b.resize(pixel_count);
	plane_a.resize(pixel_count);
	dst.assign(pixel_count * 3U, 0.0f);

	vImage_Buffer src = {
		.data = (void *)frame.image_bgra.data(),
		.height = frame.image_height,
		.width = frame.image_width,
		.rowBytes = frame.image_linesize,
	};
	vImage_Buffer scaled = {
		.data = resized_bgra.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width * 4U,
	};
	if (vImageScale_ARGB8888(&src, &scaled, nullptr, kvImageHighQualityResampling) != kvImageNoError)
		return false;

	vImage_Buffer a_plane = {
		.data = plane_a.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width,
	};
	vImage_Buffer r_plane = {
		.data = plane_r.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width,
	};
	vImage_Buffer g_plane = {
		.data = plane_g.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width,
	};
	vImage_Buffer b_plane = {
		.data = plane_b.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width,
	};
	if (vImageConvert_BGRA8888toPlanar8(&scaled, &b_plane, &g_plane, &r_plane, &a_plane,
					      kvImageNoFlags) != kvImageNoError) {
		return false;
	}

	if (nchw) {
		const size_t channel_stride = pixel_count;
		vImage_Buffer r_dest = {
			.data = dst.data() + channel_stride * 0U,
			.height = dst_height,
			.width = dst_width,
			.rowBytes = dst_width * sizeof(float),
		};
		vImage_Buffer g_dest = {
			.data = dst.data() + channel_stride * 1U,
			.height = dst_height,
			.width = dst_width,
			.rowBytes = dst_width * sizeof(float),
		};
		vImage_Buffer b_dest = {
			.data = dst.data() + channel_stride * 2U,
			.height = dst_height,
			.width = dst_width,
			.rowBytes = dst_width * sizeof(float),
		};
		if (vImageConvert_Planar8toPlanarF(&r_plane, &r_dest, 1.0f, 0.0f, kvImageNoFlags) !=
			    kvImageNoError ||
		    vImageConvert_Planar8toPlanarF(&g_plane, &g_dest, 1.0f, 0.0f, kvImageNoFlags) !=
			    kvImageNoError ||
		    vImageConvert_Planar8toPlanarF(&b_plane, &b_dest, 1.0f, 0.0f, kvImageNoFlags) !=
			    kvImageNoError) {
			return false;
		}
		return true;
	}

	plane_r_f.resize(pixel_count);
	plane_g_f.resize(pixel_count);
	plane_b_f.resize(pixel_count);
	vImage_Buffer r_f_plane = {
		.data = plane_r_f.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width * sizeof(float),
	};
	vImage_Buffer g_f_plane = {
		.data = plane_g_f.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width * sizeof(float),
	};
	vImage_Buffer b_f_plane = {
		.data = plane_b_f.data(),
		.height = dst_height,
		.width = dst_width,
		.rowBytes = dst_width * sizeof(float),
	};
	if (vImageConvert_Planar8toPlanarF(&r_plane, &r_f_plane, 1.0f, 0.0f, kvImageNoFlags) !=
		    kvImageNoError ||
	    vImageConvert_Planar8toPlanarF(&g_plane, &g_f_plane, 1.0f, 0.0f, kvImageNoFlags) !=
		    kvImageNoError ||
	    vImageConvert_Planar8toPlanarF(&b_plane, &b_f_plane, 1.0f, 0.0f, kvImageNoFlags) !=
		    kvImageNoError) {
		return false;
	}
	for (size_t i = 0; i < pixel_count; ++i) {
		const size_t dst_index = i * 3U;
		dst[dst_index + 0U] = plane_r_f[i];
		dst[dst_index + 1U] = plane_g_f[i];
		dst[dst_index + 2U] = plane_b_f[i];
	}
	return true;
}
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
				 double &preprocess_ms_out)
{
	const auto preprocess_started = std::chrono::steady_clock::now();
	bool preprocessed = false;
#if defined(__APPLE__)
	const bool prefer_accelerate =
		config.preprocess_mode != lenses::core::PreprocessMode::Scalar;
	if (prefer_accelerate) {
		preprocessed = ResizeBGRAtoRGBFloatAccelerate(
			frame, input_width, input_height, nchw, input_tensor_values, resized_bgra,
			plane_r, plane_g, plane_b, plane_a, plane_r_f, plane_g_f, plane_b_f);
	}
#else
	(void)config;
	(void)resized_bgra;
	(void)plane_r;
	(void)plane_g;
	(void)plane_b;
	(void)plane_a;
	(void)plane_r_f;
	(void)plane_g_f;
	(void)plane_b_f;
#endif
	if (!preprocessed)
		ResizeBGRAtoRGBFloatScalar(frame, input_width, input_height, nchw, input_tensor_values);

	preprocess_ms_out = std::chrono::duration<double, std::milli>(
				    std::chrono::steady_clock::now() - preprocess_started)
				    .count();
	return !input_tensor_values.empty();
}

bool TryParseCoreMLCapabilityLog(const char *message, uint32_t &out_partitions,
			       uint32_t &out_total_nodes,
			       uint32_t &out_supported_nodes)
{
	if (!message || !strstr(message, "CoreMLExecutionProvider::GetCapability"))
		return false;

	uint32_t partitions = 0;
	uint32_t total_nodes = 0;
	uint32_t supported_nodes = 0;
	if (!TryParseUintAfterLabel(message, kCoreMLCapabilityPartitionsLabel, partitions))
		return false;
	if (!TryParseUintAfterLabel(message, kCoreMLCapabilityTotalNodesLabel, total_nodes))
		return false;
	if (!TryParseUintAfterLabel(message, kCoreMLCapabilitySupportedNodesLabel, supported_nodes))
		return false;

	out_partitions = partitions;
	out_total_nodes = total_nodes;
	out_supported_nodes = supported_nodes;
	return true;
}

} // namespace lenses::ai::ort::detail
