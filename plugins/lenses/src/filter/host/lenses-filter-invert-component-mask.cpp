#include "filter/host/lenses-filter-invert-component-mask.h"

#include "filter/invert/lenses-invert-opencv-pipeline.hpp"

#include <cstring>
#include <new>
#include <vector>

namespace {

using lenses::filter::invert::OpenCvRegionMaskPipeline;
using lenses::filter::invert::OpenCvRegionDebugPreview;
using lenses::filter::invert::OpenCvInputLayout;
using lenses::filter::invert::OpenCvRegionParams;
using lenses::filter::invert::OpenCvRegionStats;

struct OpenCvPipelineHolder {
	OpenCvRegionMaskPipeline pipeline;
	std::vector<uint8_t> mask;
	std::vector<uint8_t> base_mask;
	std::vector<uint8_t> expansion_gate;
	OpenCvRegionDebugPreview debug_preview;
};

static float clampf(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static bool ensure_mask_texture(struct lenses_invert_component_mask_context *context,
				uint32_t width, uint32_t height, const uint8_t *data)
{
	if (!context || !data || width == 0 || height == 0)
		return false;

	if (context->texture &&
	    (context->width != width || context->height != height)) {
		gs_texture_destroy(context->texture);
		context->texture = NULL;
		context->width = 0;
		context->height = 0;
	}

	if (!context->texture) {
		const uint8_t *planes[1] = {data};
		context->texture =
			gs_texture_create(width, height, GS_A8, 1, planes, GS_DYNAMIC);
		if (!context->texture)
			return false;
		context->width = width;
		context->height = height;
	} else {
		gs_texture_set_image(context->texture, data, width, false);
	}

	return true;
}

static bool ensure_preview_texture(struct lenses_invert_component_mask_context *context,
				   uint32_t width, uint32_t height, const uint8_t *data)
{
	if (!context || !data || width == 0 || height == 0)
		return false;

	if (context->hue_preview_texture &&
	    (context->hue_preview_width != width || context->hue_preview_height != height)) {
		gs_texture_destroy(context->hue_preview_texture);
		context->hue_preview_texture = NULL;
		context->hue_preview_width = 0;
		context->hue_preview_height = 0;
	}

	if (!context->hue_preview_texture) {
		const uint8_t *planes[1] = {data};
		context->hue_preview_texture =
			gs_texture_create(width, height, GS_BGRA, 1, planes, GS_DYNAMIC);
		if (!context->hue_preview_texture)
			return false;
		context->hue_preview_width = width;
		context->hue_preview_height = height;
	} else {
		gs_texture_set_image(context->hue_preview_texture, data, width * 4U, false);
	}

	return true;
}

static OpenCvPipelineHolder *get_or_create_pipeline(struct lenses_invert_component_mask_context *context)
{
	if (!context)
		return nullptr;
	if (context->opencv_pipeline)
		return static_cast<OpenCvPipelineHolder *>(context->opencv_pipeline);

	OpenCvPipelineHolder *holder = new (std::nothrow) OpenCvPipelineHolder();
	if (!holder)
		return nullptr;
	context->opencv_pipeline = holder;
	return holder;
}

static void reset_stats(struct lenses_invert_component_mask_context *context)
{
	if (!context)
		return;
	context->accepted_pixels = 0;
	context->accepted_components = 0;
	context->accepted_coverage = 0.0f;
	context->hue_preview_selected_pixels = 0;
	context->hue_preview_selected_coverage = 0.0f;
	context->hue_preview_ready = false;
}

static void apply_expansion_guard(std::vector<uint8_t> &mask,
				  const std::vector<uint8_t> &base_mask,
				  const std::vector<uint8_t> &expansion_gate)
{
	const size_t pixel_count = mask.size();
	if (base_mask.size() != pixel_count || expansion_gate.size() != pixel_count)
		return;

	for (size_t i = 0; i < pixel_count; ++i) {
		const uint8_t base = base_mask[i];
		const uint8_t shaped = mask[i];
		if (shaped <= base)
			continue;

		const uint8_t gate = expansion_gate[i];
		const uint8_t delta = (uint8_t)(shaped - base);
		const uint16_t gated_delta = (uint16_t)(((uint16_t)delta * (uint16_t)gate + 127U) / 255U);
		const uint16_t restored = (uint16_t)base + gated_delta;
		mask[i] = (uint8_t)(restored > 255U ? 255U : restored);
	}
}

static bool has_nonzero_pixels(const std::vector<uint8_t> &buffer)
{
	for (uint8_t value : buffer) {
		if (value > 0)
			return true;
	}
	return false;
}

} // namespace

extern "C" void lenses_invert_component_mask_reset(struct lenses_invert_component_mask_context *context)
{
	if (!context)
		return;

	gs_texture_destroy(context->texture);
	context->texture = NULL;
	context->width = 0;
	context->height = 0;

	gs_texture_destroy(context->hue_preview_texture);
	context->hue_preview_texture = NULL;
	context->hue_preview_width = 0;
	context->hue_preview_height = 0;
	lenses_mask_shape_context_reset(&context->mask_shape_context);

	delete static_cast<OpenCvPipelineHolder *>(context->opencv_pipeline);
	context->opencv_pipeline = NULL;

	reset_stats(context);
	context->ready = false;
}

extern "C" bool lenses_invert_component_mask_update(
	struct lenses_invert_component_mask_context *context, const uint8_t *bgra, uint32_t width,
	uint32_t height, uint32_t linesize,
	const struct lenses_invert_component_mask_params *params)
{
	if (!context || !bgra || width == 0 || height == 0 || linesize < width * 4U) {
		if (context)
			context->ready = false;
		return false;
	}

	reset_stats(context);
	if (params && !params->enabled) {
		context->ready = false;
		return true;
	}

	OpenCvPipelineHolder *holder = get_or_create_pipeline(context);
	if (!holder) {
		context->ready = false;
		return false;
	}

	OpenCvRegionParams pipeline_params;
	struct lenses_mask_shape_params shape_params = {};
	if (params) {
		pipeline_params.threshold = clampf(params->threshold, 0.0f, 1.0f);
		pipeline_params.edge_softness = clampf(params->edge_softness, 0.0f, 1.0f);
		pipeline_params.topology_softness =
			clampf(params->topology_softness,
			       LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_MIN,
			       LENSES_INVERT_COMPONENT_TOPOLOGY_SOFTNESS_MAX);
		pipeline_params.coverage = clampf(params->coverage, 0.0f, 1.0f);
		pipeline_params.luma_min = clampf(params->luma_min, 0.0f, 1.0f);
		pipeline_params.luma_max = clampf(params->luma_max, 0.0f, 1.0f);
		pipeline_params.saturation_min = clampf(params->saturation_min, 0.0f, 1.0f);
		pipeline_params.saturation_max = clampf(params->saturation_max, 0.0f, 1.0f);
		pipeline_params.hue_min_degrees = clampf(params->hue_min_degrees, 0.0f, 360.0f);
		pipeline_params.hue_max_degrees = clampf(params->hue_max_degrees, 0.0f, 360.0f);
		pipeline_params.hue_qualifier = params->hue_qualifier;
		pipeline_params.min_area_px =
			clampf(params->min_area_px, LENSES_INVERT_COMPONENT_MIN_AREA_PX_MIN,
			       LENSES_INVERT_COMPONENT_MIN_AREA_PX_MAX);
		pipeline_params.min_side_px =
			clampf(params->min_side_px, LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MIN,
			       LENSES_INVERT_COMPONENT_MIN_SIDE_PX_MAX);
		pipeline_params.min_fill =
			clampf(params->min_fill, LENSES_INVERT_COMPONENT_MIN_FILL_MIN,
			       LENSES_INVERT_COMPONENT_MIN_FILL_MAX);
		pipeline_params.min_coverage =
			clampf(params->min_coverage, LENSES_INVERT_COMPONENT_MIN_COVERAGE_MIN,
			       LENSES_INVERT_COMPONENT_MIN_COVERAGE_MAX);
		pipeline_params.capture_hue_debug_preview = params->capture_hue_debug_preview;
		shape_params = params->mask_shape;
	}
	const bool apply_shape_ops = shape_params.grow_px > 0.0001f ||
				     shape_params.shrink_px > 0.0001f ||
				     shape_params.soften_px > 0.0001f;
	const bool apply_expand_guard = shape_params.grow_px > 0.0001f;

	OpenCvRegionStats stats;
	OpenCvRegionDebugPreview *debug_preview =
		(params && params->capture_hue_debug_preview) ? &holder->debug_preview : nullptr;
	const OpenCvInputLayout input_layout =
		(params && params->input_luma_only) ? OpenCvInputLayout::Luma8 : OpenCvInputLayout::Bgra8;
	holder->expansion_gate.clear();
	std::vector<uint8_t> *expansion_gate =
		apply_expand_guard ? &holder->expansion_gate : nullptr;
	if (!holder->pipeline.BuildMask(bgra, width, height, linesize, pipeline_params,
					holder->mask, stats, debug_preview, expansion_gate,
					input_layout)) {
		context->ready = false;
		return false;
	}

	const bool needs_post_shape_update = apply_shape_ops || apply_expand_guard;
	if (apply_expand_guard)
		holder->base_mask = holder->mask;
	else
		holder->base_mask.clear();

	bool any_pixels_after_shape = stats.accepted_pixels > 0;
	if (apply_shape_ops && !holder->mask.empty() &&
	    !lenses_mask_shape_apply(&context->mask_shape_context, holder->mask.data(), width, height,
				     &shape_params, &any_pixels_after_shape)) {
		context->ready = false;
		return false;
	}
	if (apply_expand_guard && !holder->mask.empty() &&
	    holder->base_mask.size() == holder->mask.size() &&
	    holder->expansion_gate.size() == holder->mask.size()) {
		apply_expansion_guard(holder->mask, holder->base_mask, holder->expansion_gate);
		any_pixels_after_shape = has_nonzero_pixels(holder->mask);
	}
	if (needs_post_shape_update) {
		if (!any_pixels_after_shape) {
			if (!holder->mask.empty())
				memset(holder->mask.data(), 0, holder->mask.size());
			stats.accepted_pixels = 0;
			stats.accepted_components = 0;
			stats.accepted_coverage = 0.0f;
		} else {
			const size_t pixel_count = holder->mask.size();
			uint32_t accepted_pixels = 0;
			for (uint8_t value : holder->mask) {
				if (value > 0)
					accepted_pixels++;
			}
			stats.accepted_pixels = accepted_pixels;
			stats.accepted_coverage =
				pixel_count > 0 ? (float)accepted_pixels / (float)pixel_count : 0.0f;
		}
	}

	const size_t pixel_count = (size_t)width * (size_t)height;
	if (holder->mask.size() != pixel_count) {
		context->ready = false;
		return false;
	}

	if (!ensure_mask_texture(context, width, height, holder->mask.data())) {
		context->ready = false;
		return false;
	}

	context->accepted_pixels = stats.accepted_pixels;
	context->accepted_components = stats.accepted_components;
	context->accepted_coverage = stats.accepted_coverage;
	if (debug_preview && debug_preview->has_hue_preview &&
	    !debug_preview->hue_preview_bgra.empty()) {
		if (!ensure_preview_texture(context, width, height,
					    debug_preview->hue_preview_bgra.data())) {
			context->ready = false;
			return false;
		}
		context->hue_preview_selected_pixels = debug_preview->hue_selected_pixels;
		context->hue_preview_selected_coverage = debug_preview->hue_selected_coverage;
		context->hue_preview_ready = true;
	}
	context->ready = true;
	return true;
}
