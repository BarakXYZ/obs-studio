#include "filter/host/lenses-filter-internal.h"

#include <inttypes.h>

void lenses_update_runtime_diagnostics(struct lenses_filter_data *filter, uint64_t now_ns)
{
	if (!filter || !filter->core)
		return;
	if (!lenses_filter_ai_lane_active(filter)) {
		filter->latest_mask_frame_id = 0;
		return;
	}

	lenses_core_try_get_latest_mask_frame_id(filter->core, &filter->latest_mask_frame_id);

	struct lenses_core_runtime_snapshot render_snapshot = {0};
	const bool has_render_snapshot = lenses_core_get_runtime_snapshot(filter->core, &render_snapshot);
	const struct lenses_core_runtime_stats *render_stats = has_render_snapshot ? &render_snapshot.stats : NULL;
	const bool has_mask_snapshot = has_render_snapshot && render_snapshot.has_latest_mask_frame;
	const struct lenses_core_mask_frame_info *render_frame_info =
		has_mask_snapshot ? &render_snapshot.latest_mask_frame : NULL;

	if (render_stats)
		lenses_maybe_downshift_auto_input_profile(filter, render_stats, now_ns);

	if (now_ns - filter->runtime_diag_last_log_ns > 2000000000ULL) {
		if (render_stats) {
			char hue_summary[96] = {0};
			lenses_hue_qualifier_format_band_summary(&filter->invert_hue_qualifier, hue_summary,
								 sizeof(hue_summary));
			const double submitted_fps = render_stats->submit_fps;
			const double completed_fps = render_stats->complete_fps;
			const double dropped_fps = render_stats->drop_fps;
			blog(LOG_INFO,
			     "[lenses] debug telemetry submitted=%" PRIu64 " completed=%" PRIu64
			     " dropped=%" PRIu64
			     " cadence_skip=%" PRIu64 " similarity_skip=%" PRIu64
			     " submit_q=%zu output_q=%zu submit_fps=%.2f complete_fps=%.2f drop_fps=%.2f latest_mask_frame_id=%" PRIu64
			     " snapshot=%d instances=%zu class_masks=%zu ai_luma=%.3f"
			     " component_gate(enabled=%d ready=%d coverage=%.3f components=%" PRIu32
			     " min_cov=%.3f luma=[%.2f,%.2f] sat=[%.2f,%.2f]"
			     " hue_qualifier=%s hue_preview=%.3f)"
			     " stage_ms(last: readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f queue=%.2f e2e=%.2f)"
			     " stage_p95(readback=%.2f preprocess=%.2f infer=%.2f decode=%.2f track=%.2f queue=%.2f e2e=%.2f)",
			     render_stats->submitted_frames, render_stats->completed_frames,
			     render_stats->dropped_frames, render_stats->cadence_skipped_frames,
			     render_stats->similarity_skipped_frames, render_stats->submit_queue_depth,
			     render_stats->output_queue_depth, submitted_fps, completed_fps, dropped_fps,
			     filter->latest_mask_frame_id, has_mask_snapshot ? 1 : 0,
			     has_mask_snapshot && render_frame_info ? render_frame_info->instance_count : 0U,
			     has_mask_snapshot && render_frame_info ? render_frame_info->class_mask_count : 0U,
			     filter->ai_last_frame_luma,
			     filter->invert_component_gate_enabled ? 1 : 0,
			     filter->invert_component_mask.ready ? 1 : 0,
			     filter->invert_component_mask.accepted_coverage,
			     filter->invert_component_mask.accepted_components,
			     filter->invert_component_min_coverage,
			     filter->invert_region.luma_min, filter->invert_region.luma_max,
			     filter->invert_region.saturation_min,
			     filter->invert_region.saturation_max, hue_summary,
			     filter->invert_component_mask.hue_preview_selected_coverage,
			     render_stats->last_readback_ms,
			     render_stats->last_preprocess_ms, render_stats->last_infer_ms,
			     render_stats->last_decode_ms, render_stats->last_track_ms,
			     render_stats->last_queue_latency_ms,
			     render_stats->last_end_to_end_latency_ms,
			     render_stats->readback_ms_p95, render_stats->preprocess_ms_p95,
			     render_stats->infer_ms_p95, render_stats->decode_ms_p95,
			     render_stats->track_ms_p95,
			     render_stats->queue_latency_ms_p95,
			     render_stats->end_to_end_latency_ms_p95);
			filter->runtime_prev_stats_ns = now_ns;
			filter->runtime_prev_submitted_frames = render_stats->submitted_frames;
			filter->runtime_prev_completed_frames = render_stats->completed_frames;
			filter->runtime_prev_dropped_frames = render_stats->dropped_frames;
		}
		filter->runtime_diag_last_log_ns = now_ns;
	}

	if (now_ns - filter->runtime_hint_last_log_ns > 10000000000ULL) {
		if (render_stats && render_stats->completed_frames > 0 && has_mask_snapshot &&
		    render_frame_info && render_frame_info->instance_count == 0) {
			blog(LOG_INFO,
			     "[lenses] model active but detected 0 COCO instances in recent frames. "
			     "Try content with people/vehicles/animals or switch backend/model.");
		}
		filter->runtime_hint_last_log_ns = now_ns;
	}

	if (now_ns - filter->runtime_perf_hint_last_log_ns > 10000000000ULL) {
		if (render_stats && render_stats->completed_frames >= 10 && filter->ai_target_fps > 0) {
			const double infer_ceiling_fps = lenses_ai_infer_ceiling_fps(render_stats);
			const double target_fps = (double)filter->ai_target_fps;
			if (infer_ceiling_fps > 0.0 && infer_ceiling_fps + 0.2 < target_fps) {
				blog(LOG_WARNING,
				     "[lenses] AI target exceeds current inference capacity: target_fps=%.2f ceiling_fps=%.2f infer_ms_p95=%.2f input=%ux%u model='%s'. "
				     "Use lower input profile or smaller model tier for higher realtime cadence.",
				     target_fps, infer_ceiling_fps, render_stats->infer_ms_p95,
				     filter->ai_input_width, filter->ai_input_height,
				     filter->ai_resolved_model_name[0] ? filter->ai_resolved_model_name
								       : "unknown");
			}
		}
		filter->runtime_perf_hint_last_log_ns = now_ns;
	}
}
