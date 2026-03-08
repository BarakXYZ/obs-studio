#include "filter/host/lenses-filter-mask-shape.h"

#include <util/bmem.h>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

static bool ensure_u8_buffer(uint8_t **buffer, size_t *capacity, size_t required)
{
	if (!buffer || !capacity)
		return false;
	if (*capacity >= required)
		return true;

	uint8_t *resized = static_cast<uint8_t *>(brealloc(*buffer, required));
	if (!resized)
		return false;

	*buffer = resized;
	*capacity = required;
	return true;
}

static bool ensure_shape_buffers(struct lenses_mask_shape_context *ctx, size_t pixel_count)
{
	if (!ctx)
		return false;

	return ensure_u8_buffer(&ctx->scratch_a, &ctx->scratch_a_capacity, pixel_count) &&
	       ensure_u8_buffer(&ctx->scratch_b, &ctx->scratch_b_capacity, pixel_count) &&
	       ensure_u8_buffer(&ctx->scratch_c, &ctx->scratch_c_capacity, pixel_count);
}

static float clampf(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static bool apply_fractional_morphology(struct lenses_mask_shape_context *ctx, cv::Mat &mask_view,
						 bool dilate, float amount)
{
	if (!ctx || mask_view.empty() || amount <= 0.0001f)
		return true;

	const size_t pixel_count = (size_t)mask_view.cols * (size_t)mask_view.rows;
	if (!ensure_shape_buffers(ctx, pixel_count))
		return false;

	cv::Mat current(mask_view.rows, mask_view.cols, CV_8UC1, ctx->scratch_a);
	cv::Mat candidate(mask_view.rows, mask_view.cols, CV_8UC1, ctx->scratch_b);
	mask_view.copyTo(current);

	const int whole_steps = (int)std::floor(amount);
	const float fractional_step = amount - (float)whole_steps;
	const cv::Mat kernel =
		cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));

	if (whole_steps > 0) {
		if (dilate) {
			cv::dilate(current, current, kernel, cv::Point(-1, -1), whole_steps,
				  cv::BORDER_REPLICATE);
		} else {
			cv::erode(current, current, kernel, cv::Point(-1, -1), whole_steps,
				 cv::BORDER_REPLICATE);
		}
	}

	if (fractional_step > 0.0001f) {
		if (dilate) {
			cv::dilate(current, candidate, kernel, cv::Point(-1, -1), 1,
				  cv::BORDER_REPLICATE);
		} else {
			cv::erode(current, candidate, kernel, cv::Point(-1, -1), 1,
				 cv::BORDER_REPLICATE);
		}
		cv::addWeighted(current, 1.0 - (double)fractional_step, candidate,
			      (double)fractional_step, 0.0, current);
	}

	current.copyTo(mask_view);
	return true;
}

static int odd_kernel_from_radius(int radius)
{
	if (radius <= 0)
		return 1;
	return radius * 2 + 1;
}

static bool apply_fractional_soften(struct lenses_mask_shape_context *ctx, cv::Mat &mask_view,
					    float amount)
{
	if (!ctx || mask_view.empty() || amount <= 0.0001f)
		return true;

	const size_t pixel_count = (size_t)mask_view.cols * (size_t)mask_view.rows;
	if (!ensure_shape_buffers(ctx, pixel_count))
		return false;

	cv::Mat low(mask_view.rows, mask_view.cols, CV_8UC1, ctx->scratch_a);
	cv::Mat high(mask_view.rows, mask_view.cols, CV_8UC1, ctx->scratch_b);

	const int low_radius = (int)std::floor(amount);
	const float fractional = amount - (float)low_radius;

	if (low_radius > 0) {
		const int low_kernel = odd_kernel_from_radius(low_radius);
		cv::GaussianBlur(mask_view, low, cv::Size(low_kernel, low_kernel), 0.0, 0.0,
				 cv::BORDER_REPLICATE);
	} else {
		mask_view.copyTo(low);
	}

	if (fractional > 0.0001f) {
		const int high_radius = low_radius + 1;
		const int high_kernel = odd_kernel_from_radius(high_radius);
		cv::GaussianBlur(mask_view, high, cv::Size(high_kernel, high_kernel), 0.0, 0.0,
				 cv::BORDER_REPLICATE);
		cv::addWeighted(low, 1.0 - (double)fractional, high, (double)fractional, 0.0,
			      mask_view);
	} else {
		low.copyTo(mask_view);
	}

	return true;
}

} // namespace

extern "C" void lenses_mask_shape_context_reset(struct lenses_mask_shape_context *ctx)
{
	if (!ctx)
		return;

	bfree(ctx->scratch_a);
	ctx->scratch_a = NULL;
	ctx->scratch_a_capacity = 0;
	bfree(ctx->scratch_b);
	ctx->scratch_b = NULL;
	ctx->scratch_b_capacity = 0;
	bfree(ctx->scratch_c);
	ctx->scratch_c = NULL;
	ctx->scratch_c_capacity = 0;
}

extern "C" bool lenses_mask_shape_apply(struct lenses_mask_shape_context *ctx, uint8_t *mask,
						 uint32_t width, uint32_t height,
						 const struct lenses_mask_shape_params *params,
						 bool *out_any_pixels)
{
	if (!ctx || !mask || width == 0 || height == 0)
		return false;

	struct lenses_mask_shape_params p = {
		.grow_px = 0.0f,
		.shrink_px = 0.0f,
		.soften_px = 0.0f,
	};
	if (params)
		p = *params;

	p.grow_px = clampf(p.grow_px, 0.0f, LENSES_MASK_GROW_MAX_PX);
	p.shrink_px = clampf(p.shrink_px, 0.0f, LENSES_MASK_SHRINK_MAX_PX);
	p.soften_px = clampf(p.soften_px, 0.0f, LENSES_MASK_SOFTEN_MAX_PX);

	try {
		cv::Mat mask_view((int)height, (int)width, CV_8UC1, mask);
		if (p.grow_px > 0.0001f &&
		    !apply_fractional_morphology(ctx, mask_view, true, p.grow_px))
			return false;
		if (p.shrink_px > 0.0001f &&
		    !apply_fractional_morphology(ctx, mask_view, false, p.shrink_px))
			return false;
		if (p.soften_px > 0.0001f &&
		    !apply_fractional_soften(ctx, mask_view, p.soften_px))
			return false;

		if (out_any_pixels)
			*out_any_pixels = cv::countNonZero(mask_view) > 0;

		return true;
	} catch (...) {
		return false;
	}
}
