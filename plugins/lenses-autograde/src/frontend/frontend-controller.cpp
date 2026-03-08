#include "lenses-autograde/frontend-controller.hpp"

#include "frontend/region-selection-dialog.hpp"
#include "lenses-autograde/settings-keys.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include <QImage>
#include <QMainWindow>
#include <QMetaObject>
#include <QWidget>

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lenses_autograde::frontend {
namespace {

constexpr const char *kHotkeyName = "LensesAutograde.SelectRegion";
constexpr const char *kSaveRoot = "lenses-autograde";
constexpr const char *kSaveHotkey = "select-region-hotkey";

struct SnapshotPayload {
	std::string uuid;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t linesize = 0;
	std::vector<uint8_t> bgra;
};

class FrontendController final {
public:
	void Initialize()
	{
		std::scoped_lock lock(mutex_);
		if (initialized_)
			return;

		select_hotkey_ = obs_hotkey_register_frontend(
			kHotkeyName, obs_module_text("LensesAutograde.SelectRegionHotkey"),
			&FrontendController::OnSelectHotkeyPressed, this);
		LoadDefaultHotkeyLocked();

		obs_frontend_add_save_callback(&FrontendController::OnSaveLoad, this);
		initialized_ = true;
		blog(LOG_INFO, "[lenses-autograde] frontend controller initialized");
	}

	void Shutdown()
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_)
			return;

		obs_frontend_remove_save_callback(&FrontendController::OnSaveLoad, this);
		if (select_hotkey_ != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(select_hotkey_);
		select_hotkey_ = OBS_INVALID_HOTKEY_ID;

		for (auto &entry : instances_) {
			if (entry.second)
				obs_weak_source_release(entry.second);
		}
		instances_.clear();
		active_uuid_.clear();
		initialized_ = false;
	}

	void RegisterFilterInstance(const std::string &uuid, obs_source_t *source)
	{
		if (uuid.empty() || !source)
			return;

		std::scoped_lock lock(mutex_);
		obs_weak_source_t *weak = obs_source_get_weak_source(source);
		if (!weak)
			return;

		auto existing = instances_.find(uuid);
		if (existing != instances_.end()) {
			if (existing->second)
				obs_weak_source_release(existing->second);
			existing->second = weak;
		} else {
			instances_.emplace(uuid, weak);
		}

		if (active_uuid_.empty())
			active_uuid_ = uuid;
	}

	void UnregisterFilterInstance(const std::string &uuid)
	{
		if (uuid.empty())
			return;

		std::scoped_lock lock(mutex_);
		auto it = instances_.find(uuid);
		if (it == instances_.end())
			return;

		if (it->second)
			obs_weak_source_release(it->second);
		instances_.erase(it);

		if (active_uuid_ == uuid) {
			if (!instances_.empty())
				active_uuid_ = instances_.begin()->first;
			else
				active_uuid_.clear();
		}
	}

	void SetActiveInstance(const std::string &uuid)
	{
		if (uuid.empty())
			return;
		std::scoped_lock lock(mutex_);
		if (instances_.find(uuid) == instances_.end())
			return;
		active_uuid_ = uuid;
	}

	bool RequestSelectionForActive()
	{
		obs_source_t *source = nullptr;
		std::string uuid;
		{
			std::scoped_lock lock(mutex_);
			uuid = active_uuid_;
			source = AcquireSourceLocked(active_uuid_);
		}

		if (!source) {
			blog(LOG_WARNING,
			     "[lenses-autograde] select-region requested but no active instance is armed");
			return false;
		}

		const bool requested = BumpNonceOnSource(source, kSettingSelectRegionNonce);
		if (requested) {
			blog(LOG_INFO,
			     "[lenses-autograde] select-region request submitted for instance=%s",
			     uuid.c_str());
		}
		obs_source_release(source);
		return requested;
	}

	void SubmitSourceSnapshot(const std::string &uuid, uint32_t width, uint32_t height,
				    uint32_t linesize, const uint8_t *bgra, size_t bgra_size)
	{
		if (uuid.empty() || !bgra || width == 0 || height == 0 || linesize < width * 4 ||
		    bgra_size < (size_t)linesize * (size_t)height) {
			return;
		}

		const auto payload = std::make_shared<SnapshotPayload>();
		payload->uuid = uuid;
		payload->width = width;
		payload->height = height;
		payload->linesize = linesize;
		payload->bgra.assign(bgra, bgra + bgra_size);

		QWidget *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (!main_window) {
			HandleSnapshotOnUi(payload);
			return;
		}

		QMetaObject::invokeMethod(
			main_window,
			[this, payload]() {
				HandleSnapshotOnUi(payload);
			},
			Qt::QueuedConnection);
	}

private:
	static void OnSelectHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
	{
		if (!pressed || !data)
			return;
		static_cast<FrontendController *>(data)->RequestSelectionForActive();
	}

	static void OnSaveLoad(obs_data_t *save_data, bool saving, void *data)
	{
		if (!data)
			return;
		static_cast<FrontendController *>(data)->SaveOrLoadHotkey(save_data, saving);
	}

	void SaveOrLoadHotkey(obs_data_t *save_data, bool saving)
	{
		if (!save_data)
			return;

		std::scoped_lock lock(mutex_);
		if (select_hotkey_ == OBS_INVALID_HOTKEY_ID)
			return;

		if (saving) {
			obs_data_t *obj = obs_data_create();
			obs_data_array_t *hotkey = obs_hotkey_save(select_hotkey_);
			if (hotkey) {
				obs_data_set_array(obj, kSaveHotkey, hotkey);
				obs_data_array_release(hotkey);
			}
			obs_data_set_obj(save_data, kSaveRoot, obj);
			obs_data_release(obj);
			return;
		}

		obs_data_t *obj = obs_data_get_obj(save_data, kSaveRoot);
		if (!obj) {
			LoadDefaultHotkeyLocked();
			return;
		}

		obs_data_array_t *hotkey = obs_data_get_array(obj, kSaveHotkey);
		if (hotkey) {
			obs_hotkey_load(select_hotkey_, hotkey);
			obs_data_array_release(hotkey);
		} else {
			LoadDefaultHotkeyLocked();
		}
		obs_data_release(obj);
	}

	void LoadDefaultHotkeyLocked()
	{
		if (select_hotkey_ == OBS_INVALID_HOTKEY_ID)
			return;

		obs_key_combination_t combo = {};
		combo.key = OBS_KEY_M;
		combo.modifiers = INTERACT_COMMAND_KEY | INTERACT_SHIFT_KEY;
		obs_hotkey_load_bindings(select_hotkey_, &combo, 1);
	}

	obs_source_t *AcquireSourceLocked(const std::string &uuid) const
	{
		auto it = instances_.find(uuid);
		if (it == instances_.end() || !it->second)
			return nullptr;
		return obs_weak_source_get_source(it->second);
	}

	static bool BumpNonceOnSource(obs_source_t *source, const char *key)
	{
		if (!source || !key)
			return false;

		obs_data_t *settings = obs_source_get_settings(source);
		if (!settings)
			return false;
		const int64_t value = obs_data_get_int(settings, key);
		obs_data_set_int(settings, key, value + 1);
		obs_source_update(source, settings);
		obs_data_release(settings);
		return true;
	}

	static std::optional<NormalizedRegion> GetInitialSelectionFromSource(obs_source_t *source)
	{
		if (!source)
			return std::nullopt;

		obs_data_t *settings = obs_source_get_settings(source);
		if (!settings)
			return std::nullopt;

		const bool region_selected = obs_data_get_bool(settings, kSettingRegionSelected);
		if (!region_selected) {
			obs_data_release(settings);
			return std::nullopt;
		}

		NormalizedRegion region;
		region.x = (float)obs_data_get_double(settings, kSettingRegionX);
		region.y = (float)obs_data_get_double(settings, kSettingRegionY);
		region.width = (float)obs_data_get_double(settings, kSettingRegionWidth);
		region.height = (float)obs_data_get_double(settings, kSettingRegionHeight);
		obs_data_release(settings);

		region.x = std::clamp(region.x, 0.0f, 1.0f);
		region.y = std::clamp(region.y, 0.0f, 1.0f);
		region.width = std::clamp(region.width, 0.0f, 1.0f);
		region.height = std::clamp(region.height, 0.0f, 1.0f);
		if (region.x + region.width > 1.0f)
			region.x = 1.0f - region.width;
		if (region.y + region.height > 1.0f)
			region.y = 1.0f - region.height;
		if (region.width < 0.001f || region.height < 0.001f)
			return std::nullopt;

		/* Start with no selection if current ROI is full-frame to avoid drag/resize trap. */
		constexpr float kFullFrameEpsilon = 0.002f;
		const bool full_frame =
			region.x <= kFullFrameEpsilon && region.y <= kFullFrameEpsilon &&
			region.width >= (1.0f - kFullFrameEpsilon) &&
			region.height >= (1.0f - kFullFrameEpsilon);
		if (full_frame)
			return std::nullopt;

		return region;
	}

	static void SetSelectionOnSource(obs_source_t *source, const NormalizedRegion &region)
	{
		if (!source)
			return;

		obs_data_t *settings = obs_source_get_settings(source);
		if (!settings)
			return;

		obs_data_set_bool(settings, kSettingRegionEnabled, true);
		constexpr float kEpsilon = 0.002f;
		const bool full_frame = region.x <= kEpsilon && region.y <= kEpsilon &&
					region.width >= (1.0f - kEpsilon) &&
					region.height >= (1.0f - kEpsilon);
		obs_data_set_bool(settings, kSettingRegionSelected, !full_frame);
		obs_data_set_double(settings, kSettingRegionX, (double)region.x);
		obs_data_set_double(settings, kSettingRegionY, (double)region.y);
		obs_data_set_double(settings, kSettingRegionWidth, (double)region.width);
		obs_data_set_double(settings, kSettingRegionHeight, (double)region.height);

		if (!full_frame) {
			const int64_t analyze_nonce = obs_data_get_int(settings, kSettingAnalyzeNonce);
			obs_data_set_int(settings, kSettingAnalyzeNonce, analyze_nonce + 1);
		} else {
			blog(LOG_INFO,
			     "[lenses-autograde] full-frame ROI ignored; select a smaller area");
		}
		obs_source_update(source, settings);
		obs_data_release(settings);
	}

	void HandleSnapshotOnUi(const std::shared_ptr<SnapshotPayload> &payload)
	{
		if (!payload)
			return;

		std::string active_uuid;
		obs_source_t *source = nullptr;
		{
			std::scoped_lock lock(mutex_);
			active_uuid = active_uuid_;
			source = AcquireSourceLocked(payload->uuid);
		}

		if (!source)
			return;
		if (payload->uuid != active_uuid) {
			obs_source_release(source);
			return;
		}

		QImage image((const uchar *)payload->bgra.data(), (int)payload->width,
			     (int)payload->height, (int)payload->linesize, QImage::Format_ARGB32);
		if (image.isNull()) {
			obs_source_release(source);
			return;
		}
		image = image.copy();

		QWidget *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
		const auto initial_region = GetInitialSelectionFromSource(source);
		RegionSelectionDialog dialog(image, initial_region, parent);
		const int code = dialog.exec();
		if (code == QDialog::Accepted) {
			auto selection = dialog.Selection();
			if (selection.has_value()) {
				SetSelectionOnSource(source, *selection);
				blog(LOG_INFO,
				     "[lenses-autograde] region updated x=%.4f y=%.4f w=%.4f h=%.4f",
				     selection->x, selection->y, selection->width, selection->height);
			}
		}

		obs_source_release(source);
	}

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::string, obs_weak_source_t *> instances_;
	std::string active_uuid_;
	obs_hotkey_id select_hotkey_ = OBS_INVALID_HOTKEY_ID;
	bool initialized_ = false;
};

FrontendController &GetController()
{
	static FrontendController controller;
	return controller;
}

} // namespace

void Initialize()
{
	GetController().Initialize();
}

void Shutdown()
{
	GetController().Shutdown();
}

void RegisterFilterInstance(const std::string &uuid, obs_source_t *source)
{
	GetController().RegisterFilterInstance(uuid, source);
}

void UnregisterFilterInstance(const std::string &uuid)
{
	GetController().UnregisterFilterInstance(uuid);
}

void SetActiveInstance(const std::string &uuid)
{
	GetController().SetActiveInstance(uuid);
}

bool RequestSelectionForActive()
{
	return GetController().RequestSelectionForActive();
}

void SubmitSourceSnapshot(const std::string &uuid, uint32_t width, uint32_t height, uint32_t linesize,
			  const uint8_t *bgra, size_t bgra_size)
{
	GetController().SubmitSourceSnapshot(uuid, width, height, linesize, bgra, bgra_size);
}

} // namespace lenses_autograde::frontend
