/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include <util/c99defs.h>

#include <cstdint>
#include <string>
#include <vector>

class QWidget;

struct WindowHitInfo {
	uint64_t windowId = 0;
	bool ownerIsCurrentProcess = false;
	bool ownerIsFrontmostApplication = false;
	int ownerProcessId = 0;
	double globalMouseX = 0.0;
	double globalMouseY = 0.0;
	char ownerName[128] = {};
	char windowTitle[256] = {};
};

/* Gets the path of obs-studio specific data files (such as locale) */
bool GetDataFilePath(const char *data, std::string &path);

std::string GetDefaultVideoSavePath();

std::vector<std::string> GetPreferredLocales();

bool IsAlwaysOnTop(QWidget *window);
void SetAlwaysOnTop(QWidget *window, bool enable);
void SetWindowInputPassthrough(QWidget *window, bool enable);
void SetWindowCaptureExcluded(QWidget *window, bool exclude);
bool SetWindowNonActivatingPanel(QWidget *window, bool enable);
uint64_t GetWindowCaptureExclusionId(QWidget *window);
bool IsPrimaryMouseButtonDown();
bool QueryWindowHitInfoAtMouseLocation(uint64_t belowWindowId, WindowHitInfo &hitInfo);
bool ActivateWindowOwnerApplication(const WindowHitInfo &hitInfo);
void DeactivateCurrentApplication();
bool ReplayPrimaryClickAt(double globalX, double globalY, int targetProcessId = 0);
bool PerformAccessibilityPressAt(double globalX, double globalY, int targetProcessId);
int GetFrontmostProcessId();

bool SetDisplayAffinitySupported(void);

bool HighContrastEnabled();

enum TaskbarOverlayStatus {
	TaskbarOverlayStatusInactive,
	TaskbarOverlayStatusActive,
	TaskbarOverlayStatusPaused,
};
void TaskbarOverlayInit();
void TaskbarOverlaySetStatus(TaskbarOverlayStatus status);

#ifdef _WIN32
class RunOnceMutex;
RunOnceMutex
#else
void
#endif
CheckIfAlreadyRunning(bool &already_running);

#ifdef _WIN32
uint32_t GetWindowsVersion();
uint32_t GetWindowsBuild();
void SetProcessPriority(const char *priority);
void SetWin32DropStyle(QWidget *window);
bool DisableAudioDucking(bool disable);

struct RunOnceMutexData;

class RunOnceMutex {
	RunOnceMutexData *data = nullptr;

public:
	RunOnceMutex(RunOnceMutexData *data_) : data(data_) {}
	RunOnceMutex(const RunOnceMutex &rom) = delete;
	RunOnceMutex(RunOnceMutex &&rom);
	~RunOnceMutex();

	RunOnceMutex &operator=(const RunOnceMutex &rom) = delete;
	RunOnceMutex &operator=(RunOnceMutex &&rom);
};

bool IsRunningOnWine();
#endif

#ifdef __APPLE__
typedef enum {
	kAudioDeviceAccess = 0,
	kVideoDeviceAccess = 1,
	kScreenCapture = 2,
	kInputMonitoring = 3
} MacPermissionType;

typedef enum {
	kPermissionNotDetermined = 0,
	kPermissionRestricted = 1,
	kPermissionDenied = 2,
	kPermissionAuthorized = 3,
} MacPermissionStatus;

void EnableOSXVSync(bool enable);
void EnableOSXDockIcon(bool enable);
bool isInBundle();
void InstallNSApplicationSubclass();
void InstallNSThreadLocks();
void disableColorSpaceConversion(QWidget *window);
void SetMacOSDarkMode(bool dark);

MacPermissionStatus CheckPermissionWithPrompt(MacPermissionType type, bool prompt_for_permission);
#define CheckPermission(x) CheckPermissionWithPrompt(x, false)
#define RequestPermission(x) CheckPermissionWithPrompt(x, true)
void OpenMacOSPrivacyPreferences(const char *tab);
#endif
