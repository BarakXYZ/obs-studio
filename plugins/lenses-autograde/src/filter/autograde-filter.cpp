#include "lenses-autograde/autograde-filter.hpp"

#include "lenses-autograde/analysis-runtime.hpp"
#include "lenses-autograde/frontend-controller.hpp"
#include "lenses-autograde/settings-keys.hpp"

#include <graphics/half.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kAnalysisInputDim = 256U;
constexpr uint32_t kLutDim = 33U;

constexpr const char *kApplyEffectPath = "effects/lenses-autograde-region-lut.effect";

constexpr const char *kSettingStatusText = "status_text";

inline float Clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

inline float Clamp(float value, float min_value, float max_value)
{
	return value < min_value ? min_value : (value > max_value ? max_value : value);
}

bool IsFullFrameRegion(float x, float y, float width, float height)
{
	constexpr float kEpsilon = 0.002f;
	return x <= kEpsilon && y <= kEpsilon && width >= (1.0f - kEpsilon) &&
	       height >= (1.0f - kEpsilon);
}

const char *StateLabel(lenses_autograde::RuntimeState state)
{
	switch (state) {
	case lenses_autograde::RuntimeState::Disabled:
		return "disabled";
	case lenses_autograde::RuntimeState::Idle:
		return "idle";
	case lenses_autograde::RuntimeState::Queued:
		return "queued";
	case lenses_autograde::RuntimeState::Running:
		return "running";
	case lenses_autograde::RuntimeState::Ready:
		return "ready";
	case lenses_autograde::RuntimeState::Error:
		return "error";
	}
	return "unknown";
}

const char *BackendLabel(lenses_autograde::RuntimeBackend backend)
{
	switch (backend) {
	case lenses_autograde::RuntimeBackend::Deterministic:
		return "deterministic";
	}
	return "unknown";
}

const char *InputTransferLabel(lenses_autograde::RuntimeInputTransfer transfer)
{
	switch (transfer) {
	case lenses_autograde::RuntimeInputTransfer::SrgbNonlinear:
		return "srgb_nl";
	case lenses_autograde::RuntimeInputTransfer::Linear:
		return "linear";
	}
	return "unknown";
}

const char *GetTechniqueAndMultiplier(enum gs_color_space current_space,
				      enum gs_color_space source_space, float *multiplier)
{
	const char *tech_name = "Draw";
	*multiplier = 1.f;

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
		if (current_space == GS_CS_709_SCRGB) {
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		}
		break;
	case GS_CS_709_EXTENDED:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawTonemap";
			break;
		case GS_CS_709_SCRGB:
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
			break;
		default:
			break;
		}
		break;
	case GS_CS_709_SCRGB:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawMultiplyTonemap";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		case GS_CS_709_EXTENDED:
			tech_name = "DrawMultiply";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		default:
			break;
		}
		break;
	}

	return tech_name;
}

struct RegionRect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 1.0f;
	float height = 1.0f;
};

class FilterInstance final {
public:
	FilterInstance(obs_data_t *settings, obs_source_t *source) : source_(source)
	{
		const char *uuid = source_ ? obs_source_get_uuid(source_) : nullptr;
		source_uuid_ = uuid && uuid[0] ? uuid : "lenses-autograde-unknown";
		lenses_autograde::frontend::RegisterFilterInstance(source_uuid_, source_);
		lenses_autograde::frontend::SetActiveInstance(source_uuid_);
		Update(settings);
	}

	~FilterInstance()
	{
		lenses_autograde::frontend::UnregisterFilterInstance(source_uuid_);
		DestroyGraphicsResources();
	}

	void Update(obs_data_t *settings)
	{
		if (!settings)
			return;

		enabled_ = obs_data_get_bool(settings, lenses_autograde::kSettingEnabled);
		strength_ = Clamp01((float)obs_data_get_double(settings, lenses_autograde::kSettingStrength));
		region_selected_ =
			obs_data_get_bool(settings, lenses_autograde::kSettingRegionSelected);
		region_.x = Clamp01((float)obs_data_get_double(settings, lenses_autograde::kSettingRegionX));
		region_.y = Clamp01((float)obs_data_get_double(settings, lenses_autograde::kSettingRegionY));
		region_.width = Clamp((float)obs_data_get_double(settings, lenses_autograde::kSettingRegionWidth),
				     0.001f, 1.0f);
		region_.height =
			Clamp((float)obs_data_get_double(settings, lenses_autograde::kSettingRegionHeight),
			      0.001f, 1.0f);
		region_feather_ =
			Clamp((float)obs_data_get_double(settings, lenses_autograde::kSettingRegionFeather),
			      0.0f, 0.25f);

		if (region_.x + region_.width > 1.0f)
			region_.x = 1.0f - region_.width;
		if (region_.y + region_.height > 1.0f)
			region_.y = 1.0f - region_.height;
		region_.x = Clamp01(region_.x);
		region_.y = Clamp01(region_.y);
		if (region_selected_ &&
		    IsFullFrameRegion(region_.x, region_.y, region_.width, region_.height)) {
			region_selected_ = false;
			obs_data_set_bool(settings, lenses_autograde::kSettingRegionSelected, false);
			blog(LOG_INFO,
			     "[lenses-autograde] full-frame ROI rejected; select a smaller region");
		}

		const int64_t analyze_nonce =
			obs_data_get_int(settings, lenses_autograde::kSettingAnalyzeNonce);
		const int64_t select_nonce =
			obs_data_get_int(settings, lenses_autograde::kSettingSelectRegionNonce);

		if (!nonce_state_initialized_) {
			last_analyze_nonce_ = analyze_nonce;
			last_select_nonce_ = select_nonce;
			nonce_state_initialized_ = true;
		} else {
			if (analyze_nonce != last_analyze_nonce_) {
				last_analyze_nonce_ = analyze_nonce;
				manual_grade_armed_ = false;
				if (enabled_ && region_selected_) {
					analysis_pending_ = true;
				} else {
					analysis_pending_ = false;
					if (!region_selected_) {
						blog(LOG_INFO,
						     "[lenses-autograde] analyze ignored: no region selected");
					}
				}
			}

			if (select_nonce != last_select_nonce_) {
				last_select_nonce_ = select_nonce;
				selector_capture_requested_ = true;
			}
		}

		if (!enabled_)
			analysis_pending_ = false;
		if (!enabled_ || !region_selected_)
			manual_grade_armed_ = false;

		ConfigureRuntime();
		RefreshStatus();
		obs_data_set_string(settings, kSettingStatusText, status_text_.c_str());
	}

	void Render(gs_effect_t *)
	{
		++frame_counter_;
		obs_source_t *target = source_ ? obs_filter_get_target(source_) : nullptr;
		if (!target) {
			obs_source_skip_video_filter(source_);
			return;
		}

		const uint32_t width = obs_source_get_base_width(target);
		const uint32_t height = obs_source_get_base_height(target);
		if (width == 0 || height == 0) {
			obs_source_skip_video_filter(source_);
			return;
		}

		const enum gs_color_space preferred_spaces[] = {
			GS_CS_SRGB,
			GS_CS_SRGB_16F,
			GS_CS_709_EXTENDED,
		};
		const enum gs_color_space source_space =
			obs_source_get_color_space(target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
		const enum gs_color_format format = gs_get_format_from_space(source_space);

		PollRuntimeResult();

		if (selector_stage_pending_map_)
			TryMapSelectorStage();

		const bool hdr_blocked = source_space == GS_CS_709_EXTENDED;
		const bool full_frame_roi =
			IsFullFrameRegion(region_.x, region_.y, region_.width, region_.height);
		const bool should_apply = !hdr_blocked && enabled_ && region_selected_ &&
					  !full_frame_roi && manual_grade_armed_ &&
					  strength_ > 0.0001f && lut_ready_ &&
					  EnsureApplyEffect();
		if (!hdr_blocked)
			CaptureIfNeeded(target, width, height, source_space);

		if (!should_apply) {
			if (!logged_initial_gate_state_) {
				logged_initial_gate_state_ = true;
				blog(LOG_INFO,
				     "[lenses-autograde] bypassing apply (enabled=%d region_selected=%d full_frame=%d armed=%d lut_ready=%d strength=%.3f hdr=%d)",
				     enabled_ ? 1 : 0, region_selected_ ? 1 : 0, full_frame_roi ? 1 : 0,
				     manual_grade_armed_ ? 1 : 0, lut_ready_ ? 1 : 0, strength_,
				     hdr_blocked ? 1 : 0);
			}
			obs_source_skip_video_filter(source_);
		} else if (obs_source_process_filter_begin_with_color_space(source_, format, source_space,
									    OBS_ALLOW_DIRECT_RENDERING)) {
			/* Deterministic LUT data is uploaded as scene-linear RGB, matching OBS LUT filters. */
			gs_effect_set_texture_srgb(apply_param_lut_, lut_texture_);
			gs_effect_set_float(apply_param_amount_, strength_);
			gs_effect_set_float(apply_param_region_enabled_, 1.0f);
			gs_effect_set_float(apply_param_region_feather_, region_feather_);

			struct vec4 region_rect;
			vec4_set(&region_rect, region_.x, region_.y, region_.width, region_.height);
			gs_effect_set_vec4(apply_param_region_rect_, &region_rect);

			const float width_i = 1.0f / (float)kLutDim;
			const float scale = 1.0f - width_i;
			const float offset = 0.5f * width_i;
			struct vec3 clut_scale;
			struct vec3 clut_offset;
			vec3_set(&clut_scale, scale, scale, scale);
			vec3_set(&clut_offset, offset, offset, offset);
			gs_effect_set_vec3(apply_param_scale_, &clut_scale);
			gs_effect_set_vec3(apply_param_offset_, &clut_offset);

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
			obs_source_process_filter_tech_end(source_, apply_effect_, 0, 0,
							   "DrawAmount3DRegion");
			gs_blend_state_pop();
			if (!logged_apply_state_) {
				logged_apply_state_ = true;
				blog(LOG_INFO,
				     "[lenses-autograde] applying LUT (roi=%.3f,%.3f %.3fx%.3f strength=%.3f generation=%" PRIu64 ")",
				     region_.x, region_.y, region_.width, region_.height, strength_,
				     lut_generation_);
			}
		} else {
			obs_source_skip_video_filter(source_);
		}

		if (hdr_blocked && enabled_) {
			hdr_blocked_active_ = true;
			RefreshStatus();
		} else if (hdr_blocked_active_) {
			hdr_blocked_active_ = false;
			RefreshStatus();
		}
	}

	obs_properties_t *GetProperties()
	{
		obs_properties_t *props = obs_properties_create();
		obs_properties_add_bool(props, lenses_autograde::kSettingEnabled,
				       obs_module_text("LensesAutograde.Enable"));
		obs_properties_add_float_slider(props, lenses_autograde::kSettingStrength,
					       obs_module_text("LensesAutograde.Strength"), 0.0,
					       1.0, 0.01);

		obs_properties_add_button(props, "set_active_instance",
					 obs_module_text("LensesAutograde.SetActiveInstance"),
					 &FilterInstance::OnSetActiveInstanceClicked);
		obs_properties_add_button(props, "select_region",
					 obs_module_text("LensesAutograde.SelectRegion"),
					 &FilterInstance::OnSelectRegionClicked);
		obs_properties_add_button(props, "analyze_region",
					 obs_module_text("LensesAutograde.AnalyzeRegion"),
					 &FilterInstance::OnAnalyzeRegionClicked);

		obs_properties_add_float_slider(props, lenses_autograde::kSettingRegionX,
					       obs_module_text("LensesAutograde.RegionX"), 0.0,
					       1.0, 0.001);
		obs_properties_add_float_slider(props, lenses_autograde::kSettingRegionY,
					       obs_module_text("LensesAutograde.RegionY"), 0.0,
					       1.0, 0.001);
		obs_properties_add_float_slider(props, lenses_autograde::kSettingRegionWidth,
					       obs_module_text("LensesAutograde.RegionWidth"),
					       0.001, 1.0, 0.001);
		obs_properties_add_float_slider(props, lenses_autograde::kSettingRegionHeight,
					       obs_module_text("LensesAutograde.RegionHeight"),
					       0.001, 1.0, 0.001);
		obs_properties_add_float_slider(props, lenses_autograde::kSettingRegionFeather,
					       obs_module_text("LensesAutograde.RegionFeather"),
					       0.0, 0.25, 0.001);

		obs_property_t *status = obs_properties_add_text(
			props, kSettingStatusText, obs_module_text("LensesAutograde.Status"), OBS_TEXT_INFO);
		obs_property_text_set_info_type(status, OBS_TEXT_INFO_WARNING);

		return props;
	}

	static void Defaults(obs_data_t *settings)
	{
		obs_data_set_default_bool(settings, lenses_autograde::kSettingEnabled, true);
		obs_data_set_default_double(settings, lenses_autograde::kSettingStrength, 1.0);
		obs_data_set_default_bool(settings, lenses_autograde::kSettingRegionEnabled, true);
		obs_data_set_default_bool(settings, lenses_autograde::kSettingRegionSelected, false);
		obs_data_set_default_double(settings, lenses_autograde::kSettingRegionX, 0.0);
		obs_data_set_default_double(settings, lenses_autograde::kSettingRegionY, 0.0);
		obs_data_set_default_double(settings, lenses_autograde::kSettingRegionWidth, 1.0);
		obs_data_set_default_double(settings, lenses_autograde::kSettingRegionHeight, 1.0);
		obs_data_set_default_double(settings, lenses_autograde::kSettingRegionFeather, 0.02);
		obs_data_set_default_int(settings, lenses_autograde::kSettingAnalyzeNonce, 0);
		obs_data_set_default_int(settings, lenses_autograde::kSettingSelectRegionNonce, 0);
		obs_data_set_default_string(settings, kSettingStatusText, "Idle");
	}

	static enum gs_color_space GetColorSpace(void *data, size_t count,
					 const enum gs_color_space *preferred_spaces)
	{
		auto *filter = static_cast<FilterInstance *>(data);
		obs_source_t *target = filter && filter->source_ ? obs_filter_get_target(filter->source_) : nullptr;
		if (!target)
			return count > 0 ? preferred_spaces[0] : GS_CS_SRGB;

		const enum gs_color_space potential_spaces[] = {
			GS_CS_SRGB,
			GS_CS_SRGB_16F,
			GS_CS_709_EXTENDED,
		};
		const enum gs_color_space source_space =
			obs_source_get_color_space(target, OBS_COUNTOF(potential_spaces), potential_spaces);

		if (!count)
			return source_space;
		for (size_t i = 0; i < count; ++i) {
			if (preferred_spaces[i] == source_space)
				return source_space;
		}
		return preferred_spaces[0];
	}

private:
	static bool OnAnalyzeRegionClicked(obs_properties_t *, obs_property_t *, void *data)
	{
		auto *filter = static_cast<FilterInstance *>(data);
		if (!filter || !filter->source_)
			return false;

		obs_data_t *settings = obs_source_get_settings(filter->source_);
		if (!settings)
			return false;
		const int64_t nonce = obs_data_get_int(settings, lenses_autograde::kSettingAnalyzeNonce);
		obs_data_set_int(settings, lenses_autograde::kSettingAnalyzeNonce, nonce + 1);
		obs_source_update(filter->source_, settings);
		obs_data_release(settings);
		return true;
	}

	static bool OnSetActiveInstanceClicked(obs_properties_t *, obs_property_t *, void *data)
	{
		auto *filter = static_cast<FilterInstance *>(data);
		if (!filter)
			return false;
		lenses_autograde::frontend::SetActiveInstance(filter->source_uuid_);
		return true;
	}

	static bool OnSelectRegionClicked(obs_properties_t *, obs_property_t *, void *data)
	{
		auto *filter = static_cast<FilterInstance *>(data);
		if (!filter)
			return false;

		lenses_autograde::frontend::SetActiveInstance(filter->source_uuid_);
		return lenses_autograde::frontend::RequestSelectionForActive();
	}

	void DestroyGraphicsResources()
	{
		obs_enter_graphics();
		gs_texrender_destroy(capture_full_texrender_);
		capture_full_texrender_ = nullptr;
		gs_texrender_destroy(capture_analysis_texrender_);
		capture_analysis_texrender_ = nullptr;
		gs_stagesurface_destroy(selector_stage_surface_);
		selector_stage_surface_ = nullptr;
		gs_voltexture_destroy(lut_texture_);
		lut_texture_ = nullptr;
		gs_effect_destroy(apply_effect_);
		apply_effect_ = nullptr;
		apply_param_lut_ = nullptr;
		apply_param_amount_ = nullptr;
		apply_param_scale_ = nullptr;
		apply_param_offset_ = nullptr;
		apply_param_region_rect_ = nullptr;
		apply_param_region_enabled_ = nullptr;
		apply_param_region_feather_ = nullptr;
		obs_leave_graphics();
	}

	bool EnsureApplyEffect()
	{
		if (apply_effect_)
			return true;

		char *effect_path = obs_module_file(kApplyEffectPath);
		if (!effect_path)
			return false;

		apply_effect_ = gs_effect_create_from_file(effect_path, nullptr);
		bfree(effect_path);
		if (!apply_effect_)
			return false;

		apply_param_lut_ = gs_effect_get_param_by_name(apply_effect_, "clut_3d");
		apply_param_amount_ = gs_effect_get_param_by_name(apply_effect_, "clut_amount");
		apply_param_scale_ = gs_effect_get_param_by_name(apply_effect_, "clut_scale");
		apply_param_offset_ = gs_effect_get_param_by_name(apply_effect_, "clut_offset");
		apply_param_region_rect_ = gs_effect_get_param_by_name(apply_effect_, "region_rect");
		apply_param_region_enabled_ =
			gs_effect_get_param_by_name(apply_effect_, "region_enabled");
		apply_param_region_feather_ =
			gs_effect_get_param_by_name(apply_effect_, "region_feather");

		const bool ok = apply_param_lut_ && apply_param_amount_ && apply_param_scale_ &&
				apply_param_offset_ && apply_param_region_rect_ &&
				apply_param_region_enabled_ && apply_param_region_feather_;
		if (!ok) {
			gs_effect_destroy(apply_effect_);
			apply_effect_ = nullptr;
			apply_param_lut_ = nullptr;
			apply_param_amount_ = nullptr;
			apply_param_scale_ = nullptr;
			apply_param_offset_ = nullptr;
			apply_param_region_rect_ = nullptr;
			apply_param_region_enabled_ = nullptr;
			apply_param_region_feather_ = nullptr;
		}
		return ok;
	}

	void ConfigureRuntime()
	{
		lenses_autograde::RuntimeConfig config{};
		config.enabled = enabled_;
		config.input_width = kAnalysisInputDim;
		config.input_height = kAnalysisInputDim;

		(void)runtime_.Configure(config);
	}

	void RefreshStatus()
	{
		const auto runtime_status = runtime_.GetStatus();
		char buffer[640] = {};
		if (hdr_blocked_active_) {
			(void)snprintf(buffer, sizeof(buffer),
				      "Autograde disabled for current frame: HDR/extended color space detected");
		} else {
			(void)snprintf(
				buffer, sizeof(buffer),
				"Autograde: %s | backend=%s | lut=%s | armed=%s | roi=%s | gen=%" PRIu64
				" | detail=%.3f | infer=%.2fms total=%.2fms\n%s",
				StateLabel(runtime_status.state), BackendLabel(runtime_status.backend),
				lut_ready_ ? "ready" : "none", manual_grade_armed_ ? "yes" : "no",
				region_selected_ ? "selected" : "none", lut_generation_, detail_amount_,
				runtime_status.last_infer_ms, runtime_status.last_total_ms,
				runtime_status.detail.empty() ? "No detail" : runtime_status.detail.c_str());
		}
		status_text_ = buffer;
	}

	void PollRuntimeResult()
	{
		lenses_autograde::RuntimeResult result;
		if (!runtime_.PollResult(result)) {
			RefreshStatus();
			return;
		}

		const size_t expected_floats =
			(size_t)result.lut_dimension * (size_t)result.lut_dimension *
			(size_t)result.lut_dimension * 3U;
		const size_t voxel_count =
			(size_t)result.lut_dimension * (size_t)result.lut_dimension *
			(size_t)result.lut_dimension;
		if (result.lut_dimension != kLutDim || result.lut.size() != expected_floats) {
			blog(LOG_WARNING,
			     "[lenses-autograde] rejecting runtime LUT result with invalid shape dim=%u count=%zu",
			     result.lut_dimension, result.lut.size());
			RefreshStatus();
			return;
		}

		std::vector<half> lut_rgba(voxel_count * 4U);
		float value_min = std::numeric_limits<float>::infinity();
		float value_max = -std::numeric_limits<float>::infinity();
		uint64_t clipped_low = 0;
		uint64_t clipped_high = 0;
		for (size_t i = 0; i < voxel_count; ++i) {
			const float r_nl = result.lut[i];
			const float g_nl = result.lut[voxel_count + i];
			const float b_nl = result.lut[voxel_count * 2U + i];
			if (!std::isfinite(r_nl) || !std::isfinite(g_nl) || !std::isfinite(b_nl)) {
				blog(LOG_WARNING,
				     "[lenses-autograde] rejecting runtime LUT result with non-finite value at voxel=%zu",
				     i);
				RefreshStatus();
				return;
			}
			value_min = std::min(value_min, std::min(r_nl, std::min(g_nl, b_nl)));
			value_max = std::max(value_max, std::max(r_nl, std::max(g_nl, b_nl)));

			const float r = Clamp01(r_nl);
			const float g = Clamp01(g_nl);
			const float b = Clamp01(b_nl);
			if (r_nl < 0.0f)
				++clipped_low;
			else if (r_nl > 1.0f)
				++clipped_high;
			if (g_nl < 0.0f)
				++clipped_low;
			else if (g_nl > 1.0f)
				++clipped_high;
			if (b_nl < 0.0f)
				++clipped_low;
			else if (b_nl > 1.0f)
				++clipped_high;

			lut_rgba[i * 4U + 0U] = half_from_float(r);
			lut_rgba[i * 4U + 1U] = half_from_float(g);
			lut_rgba[i * 4U + 2U] = half_from_float(b);
			lut_rgba[i * 4U + 3U] = half_from_bits(0x3c00);
		}
		blog(LOG_INFO,
		     "[lenses-autograde] fused LUT stats backend=%s value[min=%.4f max=%.4f] clipped_low=%" PRIu64
		     " clipped_high=%" PRIu64,
		     BackendLabel(result.backend), value_min, value_max, clipped_low, clipped_high);

		const uint8_t *planes[1] = {reinterpret_cast<const uint8_t *>(lut_rgba.data())};
		gs_voltexture_destroy(lut_texture_);
		lut_texture_ = gs_voltexture_create(result.lut_dimension, result.lut_dimension,
					   result.lut_dimension, GS_RGBA16F, 1,
					   planes, 0);
		lut_ready_ = lut_texture_ != nullptr;
		if (lut_ready_) {
			lut_generation_ = result.generation;
			detail_amount_ = Clamp(result.detail_amount, 0.0f, 0.18f);
			manual_grade_armed_ = true;
		}
		RefreshStatus();
	}

	bool BuildAnalysisRoiCpuFrame(const uint8_t *video_data, uint32_t linesize)
	{
		if (!video_data || selector_stage_width_ == 0 || selector_stage_height_ == 0)
			return false;

		const uint32_t source_width = selector_stage_width_;
		const uint32_t source_height = selector_stage_height_;
		const uint32_t roi_x = (uint32_t)Clamp((float)std::floor(region_.x * source_width), 0.0f,
						       (float)(source_width - 1U));
		const uint32_t roi_y = (uint32_t)Clamp((float)std::floor(region_.y * source_height), 0.0f,
						       (float)(source_height - 1U));
		const uint32_t roi_w =
			std::max<uint32_t>(1U, (uint32_t)std::round(region_.width * source_width));
		const uint32_t roi_h =
			std::max<uint32_t>(1U, (uint32_t)std::round(region_.height * source_height));
		const uint32_t clamped_w = std::min<uint32_t>(roi_w, source_width - roi_x);
		const uint32_t clamped_h = std::min<uint32_t>(roi_h, source_height - roi_y);
		if (clamped_w == 0 || clamped_h == 0)
			return false;

		analysis_cpu_frame_.assign((size_t)kAnalysisInputDim * (size_t)kAnalysisInputDim * 4U, 0U);
		for (uint32_t y = 0; y < kAnalysisInputDim; ++y) {
			const float src_y = (float)roi_y +
					    ((float)y + 0.5f) * ((float)clamped_h / (float)kAnalysisInputDim) -
					    0.5f;
			const int y0 = std::clamp((int)std::floor(src_y), (int)roi_y,
						  (int)(roi_y + clamped_h - 1U));
			const int y1 = std::clamp(y0 + 1, (int)roi_y, (int)(roi_y + clamped_h - 1U));
			const float wy = Clamp(src_y - (float)y0, 0.0f, 1.0f);
			const uint8_t *row0 = video_data + (size_t)y0 * linesize;
			const uint8_t *row1 = video_data + (size_t)y1 * linesize;
			uint8_t *dst_row =
				analysis_cpu_frame_.data() + (size_t)y * (size_t)kAnalysisInputDim * 4U;

			for (uint32_t x = 0; x < kAnalysisInputDim; ++x) {
				const float src_x = (float)roi_x +
						    ((float)x + 0.5f) * ((float)clamped_w / (float)kAnalysisInputDim) -
						    0.5f;
				const int x0 = std::clamp((int)std::floor(src_x), (int)roi_x,
							  (int)(roi_x + clamped_w - 1U));
				const int x1 = std::clamp(x0 + 1, (int)roi_x, (int)(roi_x + clamped_w - 1U));
				const float wx = Clamp(src_x - (float)x0, 0.0f, 1.0f);

				const uint8_t *p00 = row0 + (size_t)x0 * 4U;
				const uint8_t *p10 = row0 + (size_t)x1 * 4U;
				const uint8_t *p01 = row1 + (size_t)x0 * 4U;
				const uint8_t *p11 = row1 + (size_t)x1 * 4U;
				for (uint32_t c = 0; c < 4U; ++c) {
					const float top = (1.0f - wx) * (float)p00[c] + wx * (float)p10[c];
					const float bottom = (1.0f - wx) * (float)p01[c] + wx * (float)p11[c];
					const float value = (1.0f - wy) * top + wy * bottom;
					dst_row[(size_t)x * 4U + c] =
						(uint8_t)std::lround(Clamp(value, 0.0f, 255.0f));
				}
			}
		}

		return true;
	}

	void TryMapSelectorStage()
	{
		if (!selector_stage_pending_map_ || !selector_stage_surface_)
			return;

		uint8_t *video_data = nullptr;
		uint32_t linesize = 0;
		if (!gs_stagesurface_map(selector_stage_surface_, &video_data, &linesize))
			return;

		const size_t copy_size = (size_t)linesize * (size_t)selector_stage_height_;
		if (selector_stage_emit_ui_snapshot_) {
			lenses_autograde::frontend::SubmitSourceSnapshot(
				source_uuid_, selector_stage_width_, selector_stage_height_, linesize,
				video_data, copy_size);
		}

		const bool should_submit_analysis =
			analysis_pending_ && region_selected_ &&
			!IsFullFrameRegion(region_.x, region_.y, region_.width, region_.height) &&
			!runtime_.IsBusy();
		if (should_submit_analysis && BuildAnalysisRoiCpuFrame(video_data, linesize)) {
			const uint32_t analysis_linesize = kAnalysisInputDim * 4U;
			const bool submitted = runtime_.RequestAnalysis(
				frame_counter_, kAnalysisInputDim, kAnalysisInputDim, analysis_linesize,
				analysis_cpu_frame_.data(), analysis_cpu_frame_.size(), os_gettime_ns(),
				analysis_input_transfer_);
			if (submitted) {
				analysis_pending_ = false;
				blog(LOG_INFO,
				     "[lenses-autograde] analysis submitted frame=%" PRIu64
				     " roi=%.3f,%.3f %.3fx%.3f transfer=%s (cpu-resized)",
				     frame_counter_, region_.x, region_.y, region_.width, region_.height,
				     InputTransferLabel(analysis_input_transfer_));
			}
		}

		gs_stagesurface_unmap(selector_stage_surface_);
		selector_stage_pending_map_ = false;
		selector_stage_emit_ui_snapshot_ = false;
		RefreshStatus();
	}

	void CaptureIfNeeded(obs_source_t *target, uint32_t width, uint32_t height,
			     enum gs_color_space source_space)
	{
		const bool analysis_request_active =
			analysis_pending_ && region_selected_ &&
			!IsFullFrameRegion(region_.x, region_.y, region_.width, region_.height) &&
			!runtime_.IsBusy();
		const bool needs_selector_stage =
			(selector_capture_requested_ || analysis_request_active) && !selector_stage_pending_map_;
		if (!needs_selector_stage)
			return;

		const enum gs_color_format source_format = gs_get_format_from_space(source_space);
		if (!EnsureCaptureFullTexrender(source_format) || !EnsureCaptureAnalysisTexrender())
			return;

		gs_texrender_reset(capture_full_texrender_);
		if (!gs_texrender_begin_with_color_space(capture_full_texrender_, width, height,
							 source_space))
			return;

		struct vec4 zero;
		vec4_zero(&zero);
		gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_t *parent = source_ ? obs_filter_get_parent(source_) : nullptr;
		const uint32_t parent_flags = parent ? obs_source_get_output_flags(parent) : 0;
		const bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		const bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		if (parent && target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);
		gs_blend_state_pop();

		gs_texrender_end(capture_full_texrender_);

		gs_texture_t *source_texture = gs_texrender_get_texture(capture_full_texrender_);
		if (!source_texture)
			return;
		if (!RenderCaptureToAnalysisTexture(source_texture, width, height, source_space))
			return;

		gs_texture_t *analysis_texture = gs_texrender_get_texture(capture_analysis_texrender_);
		if (!analysis_texture)
			return;
		analysis_input_transfer_ = lenses_autograde::RuntimeInputTransfer::SrgbNonlinear;
		const bool emit_ui_snapshot = selector_capture_requested_;
		StageSelectorCapture(analysis_texture, width, height, emit_ui_snapshot);
	}

	bool EnsureCaptureFullTexrender(enum gs_color_format format)
	{
		if (capture_full_texrender_ && gs_texrender_get_format(capture_full_texrender_) != format) {
			gs_texrender_destroy(capture_full_texrender_);
			capture_full_texrender_ = nullptr;
		}
		if (!capture_full_texrender_)
			capture_full_texrender_ = gs_texrender_create(format, GS_ZS_NONE);
		return capture_full_texrender_ != nullptr;
	}

	bool EnsureCaptureAnalysisTexrender()
	{
		if (capture_analysis_texrender_ &&
		    gs_texrender_get_format(capture_analysis_texrender_) != GS_BGRA) {
			gs_texrender_destroy(capture_analysis_texrender_);
			capture_analysis_texrender_ = nullptr;
		}
		if (!capture_analysis_texrender_)
			capture_analysis_texrender_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
		return capture_analysis_texrender_ != nullptr;
	}

	bool RenderCaptureToAnalysisTexture(gs_texture_t *source_texture, uint32_t width, uint32_t height,
					    enum gs_color_space source_space)
	{
		if (!source_texture || !capture_analysis_texrender_)
			return false;

		gs_texrender_reset(capture_analysis_texrender_);
		if (!gs_texrender_begin_with_color_space(capture_analysis_texrender_, width, height,
							 GS_CS_SRGB))
			return false;

		struct vec4 zero;
		vec4_zero(&zero);
		gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image_param = effect ? gs_effect_get_param_by_name(effect, "image") : nullptr;
		gs_eparam_t *multiplier_param =
			effect ? gs_effect_get_param_by_name(effect, "multiplier") : nullptr;
		if (!image_param || !multiplier_param) {
			gs_blend_state_pop();
			gs_texrender_end(capture_analysis_texrender_);
			return false;
		}

		const enum gs_color_space current_space = gs_get_color_space();
		float multiplier = 1.0f;
		const char *technique =
			GetTechniqueAndMultiplier(current_space, source_space, &multiplier);
		const bool previous_srgb = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);
		gs_effect_set_texture_srgb(image_param, source_texture);
		gs_effect_set_float(multiplier_param, multiplier);
		while (gs_effect_loop(effect, technique))
			gs_draw_sprite(source_texture, 0, width, height);
		gs_enable_framebuffer_srgb(previous_srgb);
		gs_blend_state_pop();

		gs_texrender_end(capture_analysis_texrender_);
		return true;
	}

	void StageSelectorCapture(gs_texture_t *capture_texture, uint32_t width, uint32_t height,
				  bool emit_ui_snapshot)
	{
		if (!capture_texture)
			return;

		if (!selector_stage_surface_ || selector_stage_width_ != width || selector_stage_height_ != height) {
			gs_stagesurface_destroy(selector_stage_surface_);
			selector_stage_surface_ = gs_stagesurface_create(width, height, GS_BGRA);
			selector_stage_width_ = width;
			selector_stage_height_ = height;
		}

		if (!selector_stage_surface_)
			return;

		gs_stage_texture(selector_stage_surface_, capture_texture);
		selector_stage_pending_map_ = true;
		selector_stage_emit_ui_snapshot_ = emit_ui_snapshot;
		selector_capture_requested_ = false;
	}

private:
	obs_source_t *source_ = nullptr;
	std::string source_uuid_;

	lenses_autograde::AnalysisRuntime runtime_;

	bool enabled_ = true;
	float strength_ = 1.0f;
	bool region_selected_ = false;
	bool manual_grade_armed_ = false;
	RegionRect region_{};
	float region_feather_ = 0.02f;

	int64_t last_analyze_nonce_ = 0;
	int64_t last_select_nonce_ = 0;
	bool nonce_state_initialized_ = false;
	uint64_t frame_counter_ = 0;
	bool analysis_pending_ = false;
	bool selector_capture_requested_ = false;
	bool selector_stage_pending_map_ = false;
	bool selector_stage_emit_ui_snapshot_ = false;
	bool lut_ready_ = false;
	bool hdr_blocked_active_ = false;
	bool logged_initial_gate_state_ = false;
	bool logged_apply_state_ = false;
	uint64_t lut_generation_ = 0;
	float detail_amount_ = 0.0f;
	lenses_autograde::RuntimeInputTransfer analysis_input_transfer_ =
		lenses_autograde::RuntimeInputTransfer::SrgbNonlinear;

	gs_texrender_t *capture_full_texrender_ = nullptr;
	gs_texrender_t *capture_analysis_texrender_ = nullptr;
	gs_stagesurf_t *selector_stage_surface_ = nullptr;
	uint32_t selector_stage_width_ = 0;
	uint32_t selector_stage_height_ = 0;
	std::vector<uint8_t> analysis_cpu_frame_{};

	gs_texture_t *lut_texture_ = nullptr;
	gs_effect_t *apply_effect_ = nullptr;
	gs_eparam_t *apply_param_lut_ = nullptr;
	gs_eparam_t *apply_param_amount_ = nullptr;
	gs_eparam_t *apply_param_scale_ = nullptr;
	gs_eparam_t *apply_param_offset_ = nullptr;
	gs_eparam_t *apply_param_region_rect_ = nullptr;
	gs_eparam_t *apply_param_region_enabled_ = nullptr;
	gs_eparam_t *apply_param_region_feather_ = nullptr;

	std::string status_text_ = "Autograde idle";
};

const char *FilterGetName(void *)
{
	return obs_module_text("LensesAutograde.FilterName");
}

void *FilterCreate(obs_data_t *settings, obs_source_t *source)
{
	try {
		return new FilterInstance(settings, source);
	} catch (...) {
		blog(LOG_ERROR, "[lenses-autograde] failed to create filter instance");
		return nullptr;
	}
}

void FilterDestroy(void *data)
{
	delete static_cast<FilterInstance *>(data);
}

void FilterUpdate(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<FilterInstance *>(data);
	if (filter)
		filter->Update(settings);
}

obs_properties_t *FilterProperties(void *data)
{
	auto *filter = static_cast<FilterInstance *>(data);
	return filter ? filter->GetProperties() : obs_properties_create();
}

void FilterRender(void *data, gs_effect_t *effect)
{
	auto *filter = static_cast<FilterInstance *>(data);
	if (filter)
		filter->Render(effect);
}

void FilterDefaults(obs_data_t *settings)
{
	FilterInstance::Defaults(settings);
}

enum gs_color_space FilterColorSpace(void *data, size_t count,
				     const enum gs_color_space *preferred_spaces)
{
	return FilterInstance::GetColorSpace(data, count, preferred_spaces);
}

} // namespace

struct obs_source_info lenses_autograde_filter_source = {
	.id = "lenses_autograde_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = FilterGetName,
	.create = FilterCreate,
	.destroy = FilterDestroy,
	.update = FilterUpdate,
	.get_defaults = FilterDefaults,
	.get_properties = FilterProperties,
	.video_render = FilterRender,
	.video_get_color_space = FilterColorSpace,
};
