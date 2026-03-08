#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include <obs.hpp>
#include <util/config-file.h>

#include "lenses-mirror-capture.hpp"

namespace lenses::mirror {

class MainWindowMirrorController final {
public:
	enum class EnsureResult {
		Ready,
		RetryPending,
		AttemptFailed,
	};

	~MainWindowMirrorController() { Reset(); }

	static bool IsCaptureSupported() { return GetCaptureSourceId() != nullptr; }

	EnsureResult Ensure(int screenIndex, uint64_t excludeWindowId, bool shouldShow, config_t *config)
	{
		if (source_) {
			ClearRetryState();
			SetShowing(shouldShow);
			UpdateSettings(screenIndex, excludeWindowId, false);
			return EnsureResult::Ready;
		}

		if (!CanAttemptInitNow())
			return EnsureResult::RetryPending;

		const char *sourceId = GetCaptureSourceId();
		if (!sourceId) {
			RegisterAttemptFailure();
			return EnsureResult::AttemptFailed;
		}

		if (!EnsureSceneSource(config)) {
			RegisterAttemptFailure();
			return EnsureResult::AttemptFailed;
		}

		OBSDataAutoRelease settings = obs_data_create();
		if (!settings) {
			RegisterAttemptFailure();
			return EnsureResult::AttemptFailed;
		}

		if (!ApplyCaptureSourceSettings(settings, sourceId, screenIndex, excludeWindowId)) {
			RegisterAttemptFailure();
			return EnsureResult::AttemptFailed;
		}

		if (!EnsureCaptureSource(config, sourceId, settings)) {
			RegisterAttemptFailure();
			return EnsureResult::AttemptFailed;
		}

		EnsureSceneItem();
		EnsureFilter();

		PersistSourceUuid(config, kSceneUuidConfigKey, sceneSource_);
		PersistSourceUuid(config, kCaptureUuidConfigKey, source_);

		ClearRetryState();
		screenIndex_ = screenIndex;
		excludeWindowId_ = excludeWindowId;
		SetShowing(shouldShow);
		return EnsureResult::Ready;
	}

	void UpdateSettings(int screenIndex, uint64_t excludeWindowId, bool force)
	{
		if (!source_)
			return;
		if (!force && screenIndex == screenIndex_ && excludeWindowId == excludeWindowId_)
			return;

		OBSDataAutoRelease settings = obs_data_create();
		if (!settings)
			return;

		const char *sourceId = obs_source_get_id(source_);
		if (!ApplyCaptureSourceSettings(settings, sourceId, screenIndex, excludeWindowId))
			return;

		obs_source_update(source_, settings);
		screenIndex_ = screenIndex;
		excludeWindowId_ = excludeWindowId;
	}

	void SetShowing(bool enabled)
	{
		if (!source_)
			return;

		if (enabled && !sourceShowing_) {
			obs_source_inc_showing(source_);
			sourceShowing_ = true;
		} else if (!enabled && sourceShowing_) {
			obs_source_dec_showing(source_);
			sourceShowing_ = false;
		}
	}

	void Reset()
	{
		SetShowing(false);

		if (source_) {
			obs_source_release(source_);
			source_ = nullptr;
		}

		if (sceneSource_) {
			obs_source_release(sceneSource_);
			sceneSource_ = nullptr;
		}

		ClearRetryState();
		screenIndex_ = -1;
		excludeWindowId_ = 0;
	}

	void NotifyCaptureEnvironmentChanged(bool forceRecreateSources)
	{
		if (forceRecreateSources) {
			SetShowing(false);

			if (source_) {
				obs_source_release(source_);
				source_ = nullptr;
			}

			if (sceneSource_) {
				obs_source_release(sceneSource_);
				sceneSource_ = nullptr;
			}

			screenIndex_ = -1;
			excludeWindowId_ = 0;
		}

		ClearRetryState();
	}

	int LastRetryDelayMs() const { return lastRetryDelayMs_; }

	obs_source_t *GetSource() const { return source_; }
	obs_source_t *GetSceneSource() const { return sceneSource_; }

private:
	static constexpr const char *kSceneBaseName = "Lenses Mirror Pipeline";
	static constexpr const char *kCaptureBaseName = "Lenses Mirror Capture";
	static constexpr const char *kFilterName = "Lenses";
	static constexpr const char *kConfigSection = "BasicWindow";
	static constexpr const char *kSceneUuidConfigKey = "LensesMirrorSceneUUID";
	static constexpr const char *kCaptureUuidConfigKey = "LensesMirrorSourceUUID";
	static constexpr int kRetryInitialDelayMs = 1000;
	static constexpr int kRetryMaxDelayMs = 10000;

	using Clock = std::chrono::steady_clock;

	bool sourceShowing_ = false;
	bool retryPending_ = false;
	int screenIndex_ = -1;
	uint64_t excludeWindowId_ = 0;
	int nextRetryDelayMs_ = kRetryInitialDelayMs;
	int lastRetryDelayMs_ = 0;
	Clock::time_point nextRetryAttemptTime_{};
	obs_source_t *sceneSource_ = nullptr;
	obs_source_t *source_ = nullptr;

	bool CanAttemptInitNow()
	{
		if (!retryPending_)
			return true;

		if (Clock::now() < nextRetryAttemptTime_)
			return false;

		retryPending_ = false;
		return true;
	}

	void RegisterAttemptFailure()
	{
		lastRetryDelayMs_ = nextRetryDelayMs_;
		nextRetryAttemptTime_ = Clock::now() + std::chrono::milliseconds(lastRetryDelayMs_);
		nextRetryDelayMs_ = std::min(nextRetryDelayMs_ * 2, kRetryMaxDelayMs);
		retryPending_ = true;
	}

	void ClearRetryState()
	{
		retryPending_ = false;
		nextRetryDelayMs_ = kRetryInitialDelayMs;
		lastRetryDelayMs_ = 0;
		nextRetryAttemptTime_ = Clock::time_point{};
	}

	static obs_source_t *GetSourceByStoredUuid(config_t *config, const char *key, const char *requiredId)
	{
		if (!config || !key || !requiredId)
			return nullptr;

		const char *uuid = config_get_string(config, kConfigSection, key);
		if (!uuid || !*uuid)
			return nullptr;

		obs_source_t *source = obs_get_source_by_uuid(uuid);
		if (!source)
			return nullptr;

		const char *sourceId = obs_source_get_id(source);
		if (!sourceId || strcmp(sourceId, requiredId) != 0) {
			obs_source_release(source);
			return nullptr;
		}

		return source;
	}

	static obs_source_t *GetSourceByManagedName(const char *baseName, const char *requiredId)
	{
		const std::string managedName = GetManagedName(baseName, requiredId);
		return obs_get_source_by_name(managedName.c_str());
	}

	static void PersistSourceUuid(config_t *config, const char *key, obs_source_t *source)
	{
		if (!config || !key || !source)
			return;

		const char *uuid = obs_source_get_uuid(source);
		if (!uuid || !*uuid)
			return;

		config_set_string(config, kConfigSection, key, uuid);
	}

	static std::string GetManagedName(const char *baseName, const char *requiredId)
	{
		std::string candidate = baseName;
		int suffix = 2;

		while (true) {
			obs_source_t *existing = obs_get_source_by_name(candidate.c_str());
			if (!existing)
				return candidate;

			const char *existingId = obs_source_get_id(existing);
			const bool idMatch = !requiredId || (existingId && strcmp(existingId, requiredId) == 0);
			obs_source_release(existing);

			if (idMatch)
				return candidate;

			candidate = std::string(baseName) + " (" + std::to_string(suffix++) + ")";
		}
	}

	bool EnsureSceneSource(config_t *config)
	{
		if (sceneSource_)
			return true;

		sceneSource_ = GetSourceByStoredUuid(config, kSceneUuidConfigKey, "scene");
		if (!sceneSource_)
			sceneSource_ = GetSourceByManagedName(kSceneBaseName, "scene");

		if (sceneSource_) {
			PersistSourceUuid(config, kSceneUuidConfigKey, sceneSource_);
			return true;
		}

		const std::string sceneName = GetManagedName(kSceneBaseName, "scene");
		OBSSceneAutoRelease scene = obs_scene_create(sceneName.c_str());
		if (!scene)
			return false;

		obs_source_t *sceneSource = obs_scene_get_source(scene);
		if (!sceneSource)
			return false;

		sceneSource_ = obs_source_get_ref(sceneSource);
		PersistSourceUuid(config, kSceneUuidConfigKey, sceneSource_);
		return sceneSource_ != nullptr;
	}

	bool EnsureCaptureSource(config_t *config, const char *sourceId, obs_data_t *settings)
	{
		if (source_)
			return true;

		source_ = GetSourceByStoredUuid(config, kCaptureUuidConfigKey, sourceId);
		if (!source_)
			source_ = GetSourceByManagedName(kCaptureBaseName, sourceId);

		if (source_) {
			obs_source_update(source_, settings);
			PersistSourceUuid(config, kCaptureUuidConfigKey, source_);
			return true;
		}

		const std::string sourceName = GetManagedName(kCaptureBaseName, sourceId);
		source_ = obs_source_create(sourceId, sourceName.c_str(), settings, nullptr);
		if (!source_)
			return false;

		PersistSourceUuid(config, kCaptureUuidConfigKey, source_);
		return true;
	}

	void EnsureSceneItem()
	{
		if (!sceneSource_ || !source_)
			return;

		obs_scene_t *scene = obs_scene_from_source(sceneSource_);
		if (!scene)
			return;

		const char *captureName = obs_source_get_name(source_);
		obs_sceneitem_t *item = obs_scene_find_source(scene, captureName);
		if (!item)
			item = obs_scene_add(scene, source_);

		if (item) {
			obs_sceneitem_set_visible(item, true);
			obs_sceneitem_set_locked(item, true);
		}
	}

	void EnsureFilter()
	{
		if (!source_)
			return;

		obs_source_t *filter = obs_source_get_filter_by_name(source_, kFilterName);
		if (filter) {
			obs_source_release(filter);
			return;
		}

		obs_source_t *createdFilter = obs_source_create("lenses_filter", kFilterName, nullptr, nullptr);
		if (!createdFilter)
			return;

		obs_source_filter_add(source_, createdFilter);
		obs_source_release(createdFilter);
	}
};

} // namespace lenses::mirror
