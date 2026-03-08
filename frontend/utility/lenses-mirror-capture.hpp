#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <obs.h>

#ifdef __APPLE__
#include <CoreGraphics/CGDirectDisplay.h>
#endif

namespace lenses::mirror {

inline bool InputSourceTypeAvailable(const char *id)
{
	const char *typeId = nullptr;
	for (size_t idx = 0; obs_enum_input_types(idx, &typeId); ++idx) {
		if (typeId && strcmp(typeId, id) == 0)
			return true;
	}

	return false;
}

inline const char *GetCaptureSourceId()
{
#ifdef __APPLE__
	static constexpr const char *kCandidates[] = {"screen_capture"};
#elif defined(_WIN32)
	static constexpr const char *kCandidates[] = {"monitor_capture"};
#else
	static constexpr const char *kCandidates[] = {"xshm_input_v2", "xshm_input", "pipewire-desktop-capture-source",
						      "pipewire-screen-capture-source"};
#endif

	for (const char *candidate : kCandidates) {
		if (InputSourceTypeAvailable(candidate))
			return candidate;
	}

	return nullptr;
}

inline bool ApplyCaptureSourceSettings(obs_data_t *settings, const char *sourceId, int screenIndex,
				       uint64_t excludeWindowId)
{
	if (!settings || !sourceId)
		return false;

#if !defined(__APPLE__)
	(void)excludeWindowId;
#endif

#ifdef __APPLE__
	if (strcmp(sourceId, "screen_capture") != 0)
		return false;

	uint32_t displayCount = 0;
	if (CGGetOnlineDisplayList(0, nullptr, &displayCount) != kCGErrorSuccess || displayCount == 0)
		return false;

	std::vector<CGDirectDisplayID> displays(displayCount);
	if (CGGetOnlineDisplayList(displayCount, displays.data(), &displayCount) != kCGErrorSuccess || displayCount == 0)
		return false;

	const int clampedIndex = std::clamp(screenIndex, 0, (int)displayCount - 1);
	const CGDirectDisplayID displayId = displays[(size_t)clampedIndex];

	// Only manage routing-critical fields. User-facing capture preferences
	// (cursor visibility, hide OBS, etc.) are preserved.
	obs_data_set_int(settings, "type", 0);
	obs_data_set_int(settings, "display", (int64_t)displayId);
	obs_data_set_int(settings, "lenses_exclude_window_id", (int64_t)excludeWindowId);
	return true;
#elif defined(_WIN32)
	if (strcmp(sourceId, "monitor_capture") != 0)
		return false;

	obs_data_set_int(settings, "monitor", screenIndex);
	return true;
#else
	if (strcmp(sourceId, "xshm_input_v2") == 0 || strcmp(sourceId, "xshm_input") == 0) {
		obs_data_set_int(settings, "screen", screenIndex);
		return true;
	}

	if (strcmp(sourceId, "pipewire-desktop-capture-source") == 0 ||
	    strcmp(sourceId, "pipewire-screen-capture-source") == 0) {
		return true;
	}

	return false;
#endif
}

} // namespace lenses::mirror
