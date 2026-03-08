#include "filter/host/lenses-filter-internal.h"

#include <util/platform.h>

#include <inttypes.h>

static float lenses_estimate_ai_frame_luma(const uint8_t *bgra, uint32_t width, uint32_t height,
					   uint32_t linesize)
{
	if (!bgra || width == 0 || height == 0 || linesize < width * 4U)
		return 0.0f;

	uint64_t total = 0;
	uint64_t count = 0;
	const uint32_t step = 8U;
	for (uint32_t y = 0; y < height; y += step) {
		const uint8_t *row = bgra + (size_t)y * linesize;
		for (uint32_t x = 0; x < width; x += step) {
			const uint8_t b = row[(size_t)x * 4U + 0U];
			const uint8_t g = row[(size_t)x * 4U + 1U];
			const uint8_t r = row[(size_t)x * 4U + 2U];
			const uint32_t y8 = (uint32_t)((54U * r + 183U * g + 19U * b) >> 8);
			total += y8;
			count++;
		}
	}

	if (count == 0)
		return 0.0f;
	return (float)total / (255.0f * (float)count);
}

static double lenses_ai_last_stage_ms(const struct lenses_core_runtime_stats *stats)
{
	if (!stats)
		return 0.0;

	double sum_ms = 0.0;
	if (stats->last_preprocess_ms > 0.0)
		sum_ms += stats->last_preprocess_ms;
	if (stats->last_infer_ms > 0.0)
		sum_ms += stats->last_infer_ms;
	if (stats->last_decode_ms > 0.0)
		sum_ms += stats->last_decode_ms;
	if (stats->last_track_ms > 0.0)
		sum_ms += stats->last_track_ms;

	if (sum_ms <= 0.0 && stats->last_latency_ms > 0.0)
		sum_ms = stats->last_latency_ms;
	return sum_ms;
}

static uint64_t lenses_ai_submit_interval_ns(const struct lenses_filter_data *filter,
					     const struct lenses_core_runtime_stats *stats)
{
	const uint32_t target_fps = filter && filter->ai_target_fps ? filter->ai_target_fps : 12U;
	const uint64_t base_interval_ns = 1000000000ULL / (uint64_t)(target_fps ? target_fps : 1U);

	if (!filter || !stats || filter->ai_scheduler_mode != 2U)
		return base_interval_ns;

	const double stage_ms = lenses_ai_last_stage_ms(stats);
	if (stage_ms <= 0.0)
		return base_interval_ns;

	uint64_t stage_ns = (uint64_t)(stage_ms * 1000000.0);
	/* Keep a small buffer so producer cadence tracks sustained worker throughput. */
	stage_ns += stage_ns / 8U;
	if (stage_ns < base_interval_ns)
		return base_interval_ns;
	if (stage_ns > 1000000000ULL)
		return 1000000000ULL;
	return stage_ns;
}

void lenses_submit_ai_frame(struct lenses_filter_data *filter, gs_texture_t *source_texture,
			    uint32_t source_width, uint32_t source_height,
			    enum gs_color_space source_space)
{
	if (!filter || !filter->core || !source_texture || !filter->ai_enabled ||
	    filter->ai_target_fps == 0)
		return;

	const uint64_t now_ns = os_gettime_ns();
	(void)lenses_core_get_runtime_health(filter->core, &filter->runtime_health);
	if (!filter->runtime_health.ready &&
	    !lenses_try_recover_runtime_not_ready(filter, now_ns, false)) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_WARNING,
			     "[lenses] AI frame submit skipped: runtime is not ready (%s)",
			     filter->runtime_health.detail[0] ? filter->runtime_health.detail
							      : "unknown reason");
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}

	const struct lenses_core_runtime_stats runtime_stats = lenses_core_get_runtime_stats(filter->core);
	const uint32_t submit_queue_limit =
		filter->ai_submit_queue_limit > 0 ? filter->ai_submit_queue_limit : 1U;
	const uint64_t base_interval_ns = 1000000000ULL / (uint64_t)filter->ai_target_fps;
	const uint64_t interval_ns = lenses_ai_submit_interval_ns(filter, &runtime_stats);
	if (filter->ai_scheduler_mode == 2U && interval_ns > base_interval_ns &&
	    now_ns - filter->ai_capacity_last_log_ns > 5000000000ULL) {
		const double effective_fps = 1000000000.0 / (double)interval_ns;
		blog(LOG_INFO,
		     "[lenses] adaptive scheduler engaged: target_fps=%" PRIu32 " effective_fps=%.2f"
		     " stage_ms(last: preprocess=%.2f infer=%.2f decode=%.2f track=%.2f)."
		     " Lower model tier or input size for higher AI FPS.",
		     filter->ai_target_fps, effective_fps, runtime_stats.last_preprocess_ms,
		     runtime_stats.last_infer_ms, runtime_stats.last_decode_ms,
		     runtime_stats.last_track_ms);
		filter->ai_capacity_last_log_ns = now_ns;
	}
	if (filter->last_ai_submit_ns != 0 && now_ns - filter->last_ai_submit_ns < interval_ns)
		return;

	/* Avoid expensive readback work when the async submit queue is saturated. */
	if (runtime_stats.submit_queue_depth >= submit_queue_limit) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_INFO,
			     "[lenses] AI frame submit skipped: submit queue saturated (%zu/%d)",
			     runtime_stats.submit_queue_depth, (int)submit_queue_limit);
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}

	const uint32_t ai_width = filter->ai_input_width ? filter->ai_input_width : 640;
	const uint32_t ai_height = filter->ai_input_height ? filter->ai_input_height : 640;
	if (!ai_width || !ai_height)
		return;

	gs_texrender_reset(filter->ai_input_texrender);
	if (!gs_texrender_begin(filter->ai_input_texrender, ai_width, ai_height)) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_WARNING, "[lenses] AI frame submit skipped: failed to begin AI texrender (%ux%u)",
			     ai_width, ai_height);
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}

	struct vec4 zero;
	vec4_zero(&zero);
	gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
	gs_ortho(0.0f, (float)ai_width, 0.0f, (float)ai_height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *multiplier_param = gs_effect_get_param_by_name(effect, "multiplier");
	if (!image_param || !multiplier_param) {
		gs_blend_state_pop();
		gs_texrender_end(filter->ai_input_texrender);
		return;
	}

	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier = 1.0f;
	const char *technique =
		lenses_get_technique_and_multiplier(current_space, source_space, &multiplier);
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_effect_set_texture_srgb(image_param, source_texture);
	gs_effect_set_float(multiplier_param, multiplier);
	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(source_texture, 0, ai_width, ai_height);
	gs_enable_framebuffer_srgb(previous_srgb);
	gs_blend_state_pop();
	gs_texrender_end(filter->ai_input_texrender);

	const size_t write_index = filter->ai_stage_write_index % 2U;
	const size_t read_index = (write_index + 1U) % 2U;

	for (size_t i = 0; i < 2U; ++i) {
		if (!filter->ai_stage_surfaces[i] ||
		    gs_stagesurface_get_width(filter->ai_stage_surfaces[i]) != ai_width ||
		    gs_stagesurface_get_height(filter->ai_stage_surfaces[i]) != ai_height) {
			gs_stagesurface_destroy(filter->ai_stage_surfaces[i]);
			filter->ai_stage_surfaces[i] = gs_stagesurface_create(ai_width, ai_height, GS_BGRA);
		}
	}
	if (!filter->ai_stage_surfaces[write_index] || !filter->ai_stage_surfaces[read_index]) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_WARNING, "[lenses] AI frame submit skipped: stage surfaces unavailable");
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}

	gs_stage_texture(filter->ai_stage_surfaces[write_index], gs_texrender_get_texture(filter->ai_input_texrender));

	const bool has_pending_read = filter->ai_stage_ready;
	filter->ai_stage_ready = true;
	filter->ai_stage_write_index = read_index;
	if (!has_pending_read)
		return;

	const uint64_t readback_started_ns = os_gettime_ns();
	uint8_t *video_data = NULL;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(filter->ai_stage_surfaces[read_index], &video_data, &linesize)) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_WARNING, "[lenses] AI frame submit skipped: failed to map stage surface");
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}

	const size_t required = (size_t)linesize * ai_height;
	const uint64_t readback_finished_ns = os_gettime_ns();
	const double readback_ms = readback_finished_ns > readback_started_ns
					   ? (double)(readback_finished_ns - readback_started_ns) / 1000000.0
					   : 0.0;
	filter->ai_last_readback_ms = readback_ms;
	if (required > 0 && video_data) {
		filter->ai_last_frame_luma =
			lenses_estimate_ai_frame_luma(video_data, ai_width, ai_height, linesize);
		lenses_core_submit_frame_bgra(filter->core, filter->frame_counter, source_width, source_height,
					      now_ns, ai_width, ai_height, linesize, video_data, required,
					      readback_ms);
	}
	gs_stagesurface_unmap(filter->ai_stage_surfaces[read_index]);
	if (required == 0 || !video_data) {
		if (now_ns - filter->ai_diag_last_log_ns > 2000000000ULL) {
			blog(LOG_WARNING, "[lenses] AI frame submit skipped: empty or unavailable frame buffer");
			filter->ai_diag_last_log_ns = now_ns;
		}
		return;
	}
	filter->last_ai_submit_ns = now_ns;
}
