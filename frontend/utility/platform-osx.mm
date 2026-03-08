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

#import "platform.hpp"

#import <OBSApp.hpp>

#import <util/threading.h>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <dlfcn.h>
#import <CoreGraphics/CGWindow.h>

#include <cstdio>

using namespace std;

bool isInBundle()
{
    NSRunningApplication *app = [NSRunningApplication currentApplication];
    return [app bundleIdentifier] != nil;
}

bool GetDataFilePath(const char *data, string &output)
{
    NSURL *bundleUrl = [[NSBundle mainBundle] bundleURL];
    NSString *path = [[bundleUrl path] stringByAppendingFormat:@"/%@/%s", @"Contents/Resources", data];
    output = path.UTF8String;

    return !access(output.c_str(), R_OK);
}

void CheckIfAlreadyRunning(bool &already_running)
{
    NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];

    NSUInteger appCount = [[NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId] count];

    already_running = appCount > 1;
}

string GetDefaultVideoSavePath()
{
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *url = [fm URLForDirectory:NSMoviesDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:true
                               error:nil];

    if (!url)
        return getenv("HOME");

    return url.path.fileSystemRepresentation;
}

vector<string> GetPreferredLocales()
{
    NSArray *preferred = [NSLocale preferredLanguages];

    auto locales = GetLocaleNames();
    auto lang_to_locale = [&locales](string lang) -> string {
        string lang_match = "";

        for (const auto &locale : locales) {
            if (locale.first == lang.substr(0, locale.first.size()))
                return locale.first;

            if (!lang_match.size() && locale.first.substr(0, 2) == lang.substr(0, 2))
                lang_match = locale.first;
        }

        return lang_match;
    };

    vector<string> result;
    result.reserve(preferred.count);

    for (NSString *lang in preferred) {
        string locale = lang_to_locale(lang.UTF8String);
        if (!locale.size())
            continue;

        if (find(begin(result), end(result), locale) != end(result))
            continue;

        result.emplace_back(locale);
    }

    return result;
}

bool IsAlwaysOnTop(QWidget *window)
{
    return (window->windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}

void disableColorSpaceConversion(QWidget *window)
{
    NSView *view = (__bridge NSView *) reinterpret_cast<void *>(window->winId());
    view.window.colorSpace = NSColorSpace.sRGBColorSpace;
}

void SetAlwaysOnTop(QWidget *window, bool enable)
{
    Qt::WindowFlags flags = window->windowFlags();

    if (enable) {
        NSView *view = (__bridge NSView *) reinterpret_cast<void *>(window->winId());

        [[view window] setLevel:NSScreenSaverWindowLevel];

        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }

    window->setWindowFlags(flags);
    window->show();
}

void SetWindowInputPassthrough(QWidget *window, bool enable)
{
    if (!window)
        return;

    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view || !view.window)
        return;

    [view.window setIgnoresMouseEvents:enable];
}

void SetWindowCaptureExcluded(QWidget *window, bool exclude)
{
    if (!window)
        return;

    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view || !view.window)
        return;

    // Sharing None keeps this window out of capture APIs on macOS.
    [view.window setSharingType:(exclude ? NSWindowSharingNone : NSWindowSharingReadOnly)];
}

bool SetWindowNonActivatingPanel(QWidget *window, bool enable)
{
    if (!window)
        return false;

    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view || !view.window)
        return false;

    NSWindow *nativeWindow = view.window;
    if (![nativeWindow isKindOfClass:[NSPanel class]])
        return false;

    NSUInteger styleMask = nativeWindow.styleMask;
    if (enable)
        styleMask |= NSWindowStyleMaskNonactivatingPanel;
    else
        styleMask &= ~NSWindowStyleMaskNonactivatingPanel;
    [nativeWindow setStyleMask:styleMask];

    if (enable) {
        NSWindowCollectionBehavior behavior = nativeWindow.collectionBehavior;
        behavior |= NSWindowCollectionBehaviorIgnoresCycle;
        behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
        [nativeWindow setCollectionBehavior:behavior];
    }

    return true;
}

uint64_t GetWindowCaptureExclusionId(QWidget *window)
{
    if (!window)
        return 0;

    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view || !view.window)
        return 0;

    // `windowNumber` maps to CoreGraphics window IDs used by ScreenCaptureKit.
    return (uint64_t)view.window.windowNumber;
}

bool IsPrimaryMouseButtonDown()
{
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
}

static void CopyCFStringToBuffer(CFTypeRef value, char *buffer, size_t size)
{
    if (!buffer || size == 0)
        return;

    buffer[0] = '\0';

    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return;

    CFStringGetCString((CFStringRef)value, buffer, size, kCFStringEncodingUTF8);
}

bool QueryWindowHitInfoAtMouseLocation(uint64_t belowWindowId, WindowHitInfo &hitInfo)
{
    hitInfo = {};

    CGPoint eventPoint = CGPointZero;
    CGEventRef currentEvent = CGEventCreate(NULL);
    if (currentEvent) {
        eventPoint = CGEventGetLocation(currentEvent);
        CFRelease(currentEvent);
    } else {
        NSPoint fallback = [NSEvent mouseLocation];
        eventPoint = CGPointMake(fallback.x, fallback.y);
    }

    hitInfo.globalMouseX = eventPoint.x;
    hitInfo.globalMouseY = eventPoint.y;

    NSPoint point = NSPointFromCGPoint(eventPoint);
    NSInteger windowNumber = [NSWindow windowNumberAtPoint:point
                                  belowWindowWithWindowNumber:(NSInteger)belowWindowId];
    if (windowNumber <= 0)
        return false;

    hitInfo.windowId = (uint64_t)windowNumber;

    CFArrayRef windowInfoArray =
        CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, (CGWindowID)windowNumber);
    if (!windowInfoArray)
        return true;

    if (CFArrayGetCount(windowInfoArray) > 0) {
        CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex(windowInfoArray, 0);
        if (info && CFGetTypeID(info) == CFDictionaryGetTypeID()) {
            CopyCFStringToBuffer(CFDictionaryGetValue(info, kCGWindowOwnerName), hitInfo.ownerName,
                                 sizeof(hitInfo.ownerName));
            CopyCFStringToBuffer(CFDictionaryGetValue(info, kCGWindowName), hitInfo.windowTitle,
                                 sizeof(hitInfo.windowTitle));

            CFTypeRef ownerPIDValue = CFDictionaryGetValue(info, kCGWindowOwnerPID);
            if (ownerPIDValue && CFGetTypeID(ownerPIDValue) == CFNumberGetTypeID()) {
                int ownerPID = 0;
                if (CFNumberGetValue((CFNumberRef)ownerPIDValue, kCFNumberIntType, &ownerPID)) {
                    hitInfo.ownerProcessId = ownerPID;
                    hitInfo.ownerIsCurrentProcess = ownerPID == (int)[[NSProcessInfo processInfo] processIdentifier];
                    NSRunningApplication *frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
                    hitInfo.ownerIsFrontmostApplication =
                        frontmost && ownerPID == (int)[frontmost processIdentifier];
                }
            }
        }
    }

    CFRelease(windowInfoArray);
    return true;
}

bool ActivateWindowOwnerApplication(const WindowHitInfo &hitInfo)
{
    if (hitInfo.ownerProcessId <= 0 || hitInfo.ownerIsCurrentProcess)
        return false;

    NSRunningApplication *app =
        [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)hitInfo.ownerProcessId];
    if (!app)
        return false;

    return [app activateWithOptions:(NSApplicationActivateIgnoringOtherApps | NSApplicationActivateAllWindows)];
}

void DeactivateCurrentApplication()
{
    [NSApp deactivate];
}

bool ReplayPrimaryClickAt(double globalX, double globalY, int targetProcessId)
{
    static bool warnedMissingA11y = false;
    if (!AXIsProcessTrusted()) {
        if (!warnedMissingA11y) {
            warnedMissingA11y = true;
            blog(LOG_WARNING,
                 "[lenses] Activation assist requires Accessibility permission to replay clicks");
        }
        return false;
    }

    CGPoint point = CGPointMake(globalX, globalY);
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef down = CGEventCreateMouseEvent(source, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);
    CGEventRef up = CGEventCreateMouseEvent(source, kCGEventLeftMouseUp, point, kCGMouseButtonLeft);

    if (targetProcessId > 0) {
        if (down)
            CGEventPostToPid((pid_t)targetProcessId, down);
        if (up)
            CGEventPostToPid((pid_t)targetProcessId, up);
    } else {
        if (down)
            CGEventPost(kCGHIDEventTap, down);
        if (up)
            CGEventPost(kCGHIDEventTap, up);
    }

    if (down)
        CFRelease(down);
    if (up)
        CFRelease(up);
    if (source)
        CFRelease(source);

    return down && up;
}

static bool ElementSupportsPressAction(AXUIElementRef element)
{
    if (!element)
        return false;

    CFArrayRef actions = nullptr;
    const AXError copyResult = AXUIElementCopyActionNames(element, &actions);
    if (copyResult != kAXErrorSuccess || !actions)
        return false;

    bool hasPress = false;
    const CFIndex actionCount = CFArrayGetCount(actions);
    for (CFIndex i = 0; i < actionCount; i++) {
        CFTypeRef action = CFArrayGetValueAtIndex(actions, i);
        if (action && CFGetTypeID(action) == CFStringGetTypeID() &&
            CFStringCompare((CFStringRef)action, kAXPressAction, 0) == kCFCompareEqualTo) {
            hasPress = true;
            break;
        }
    }

    CFRelease(actions);
    return hasPress;
}

static AXUIElementRef CopyParentElement(AXUIElementRef element)
{
    if (!element)
        return nullptr;

    CFTypeRef parent = nullptr;
    const AXError parentResult = AXUIElementCopyAttributeValue(element, kAXParentAttribute, &parent);
    if (parentResult != kAXErrorSuccess || !parent)
        return nullptr;

    if (CFGetTypeID(parent) != AXUIElementGetTypeID()) {
        CFRelease(parent);
        return nullptr;
    }

    return (AXUIElementRef)parent;
}

bool PerformAccessibilityPressAt(double globalX, double globalY, int targetProcessId)
{
    if (targetProcessId <= 0 || !AXIsProcessTrusted())
        return false;

    AXUIElementRef systemWide = AXUIElementCreateSystemWide();
    if (!systemWide)
        return false;

    AXUIElementRef elementAtPoint = nullptr;
    const AXError hitTestResult =
        AXUIElementCopyElementAtPosition(systemWide, (float)globalX, (float)globalY, &elementAtPoint);
    CFRelease(systemWide);
    if (hitTestResult != kAXErrorSuccess || !elementAtPoint)
        return false;

    AXUIElementRef current = elementAtPoint;
    bool handled = false;

    for (int depth = 0; current && depth < 8; depth++) {
        pid_t elementPid = 0;
        if (AXUIElementGetPid(current, &elementPid) != kAXErrorSuccess)
            elementPid = 0;

        if ((int)elementPid == targetProcessId && ElementSupportsPressAction(current)) {
            const AXError pressResult = AXUIElementPerformAction(current, kAXPressAction);
            handled = pressResult == kAXErrorSuccess;
            break;
        }

        AXUIElementRef parent = CopyParentElement(current);
        if (current != elementAtPoint)
            CFRelease(current);
        current = parent;
    }

    if (current && current != elementAtPoint)
        CFRelease(current);
    CFRelease(elementAtPoint);

    return handled;
}

int GetFrontmostProcessId()
{
    NSRunningApplication *frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (!frontmost)
        return 0;
    return (int)[frontmost processIdentifier];
}

bool SetDisplayAffinitySupported(void)
{
    // Not implemented yet
    return false;
}

typedef void (*set_int_t)(int);

void EnableOSXVSync(bool enable)
{
    static bool initialized = false;
    static bool valid = false;
    static set_int_t set_debug_options = nullptr;
    static set_int_t deferred_updates = nullptr;

    if (!initialized) {
        void *quartzCore = dlopen("/System/Library/Frameworks/"
                                  "QuartzCore.framework/QuartzCore",
                                  RTLD_LAZY);
        if (quartzCore) {
            set_debug_options = (set_int_t) dlsym(quartzCore, "CGSSetDebugOptions");
            deferred_updates = (set_int_t) dlsym(quartzCore, "CGSDeferredUpdates");

            valid = set_debug_options && deferred_updates;
        }

        initialized = true;
    }

    if (valid) {
        set_debug_options(enable ? 0 : 0x08000000);
        deferred_updates(enable ? 1 : 0);
    }
}

void EnableOSXDockIcon(bool enable)
{
    if (enable)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    else
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

@interface DockView : NSView {
      @private
    QIcon _icon;
}
@end

@implementation DockView

- (id)initWithIcon:(QIcon)icon
{
    self = [super init];
    _icon = icon;
    return self;
}

- (void)drawRect:(NSRect)dirtyRect
{
    CGSize size = dirtyRect.size;

    /* Draw regular app icon */
    NSImage *appIcon = [[NSWorkspace sharedWorkspace] iconForFile:[[NSBundle mainBundle] bundlePath]];
    [appIcon drawInRect:CGRectMake(0, 0, size.width, size.height)];

    /* Draw small icon on top */
    float iconSize = 0.45;
    CGImageRef image = _icon.pixmap(size.width, size.height).toImage().toCGImage();
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    CGContextDrawImage(
        context, CGRectMake(size.width * (1 - iconSize), 0, size.width * iconSize, size.height * iconSize), image);
    CGImageRelease(image);
}

@end

MacPermissionStatus CheckPermissionWithPrompt(MacPermissionType type, bool prompt_for_permission)
{
    __block MacPermissionStatus permissionResponse = kPermissionNotDetermined;

    switch (type) {
        case kAudioDeviceAccess: {
            AVAuthorizationStatus audioStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

            if (audioStatus == AVAuthorizationStatusNotDetermined && prompt_for_permission) {
                os_event_t *block_finished;
                os_event_init(&block_finished, OS_EVENT_TYPE_MANUAL);
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                         completionHandler:^(BOOL granted __attribute((unused))) {
                                             os_event_signal(block_finished);
                                         }];
                os_event_wait(block_finished);
                os_event_destroy(block_finished);
                audioStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
            }

            permissionResponse = (MacPermissionStatus) audioStatus;

            blog(LOG_INFO, "[macOS] Permission for audio device access %s.",
                 permissionResponse == kPermissionAuthorized ? "granted" : "denied");

            break;
        }
        case kVideoDeviceAccess: {
            AVAuthorizationStatus videoStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

            if (videoStatus == AVAuthorizationStatusNotDetermined && prompt_for_permission) {
                os_event_t *block_finished;
                os_event_init(&block_finished, OS_EVENT_TYPE_MANUAL);
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                         completionHandler:^(BOOL granted __attribute((unused))) {
                                             os_event_signal(block_finished);
                                         }];

                os_event_wait(block_finished);
                os_event_destroy(block_finished);
                videoStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
            }

            permissionResponse = (MacPermissionStatus) videoStatus;

            blog(LOG_INFO, "[macOS] Permission for video device access %s.",
                 permissionResponse == kPermissionAuthorized ? "granted" : "denied");

            break;
        }
        case kScreenCapture: {
            permissionResponse = (CGPreflightScreenCaptureAccess() ? kPermissionAuthorized : kPermissionDenied);

            if (permissionResponse != kPermissionAuthorized && prompt_for_permission) {
                permissionResponse = (CGRequestScreenCaptureAccess() ? kPermissionAuthorized : kPermissionDenied);
            }

            blog(LOG_INFO, "[macOS] Permission for screen capture %s.",
                 permissionResponse == kPermissionAuthorized ? "granted" : "denied");

            break;
        }
        case kInputMonitoring: {
            permissionResponse = (CGPreflightListenEventAccess() ? kPermissionAuthorized : kPermissionDenied);

            if (permissionResponse != kPermissionAuthorized && prompt_for_permission) {
                permissionResponse = (CGRequestListenEventAccess() ? kPermissionAuthorized : kPermissionDenied);
            }

            blog(LOG_INFO, "[macOS] Permission for input monitoring %s.",
                 permissionResponse == kPermissionAuthorized ? "granted" : "denied");
            break;
        }
    }

    return permissionResponse;
}

void OpenMacOSPrivacyPreferences(const char *tab)
{
    NSURL *url = [NSURL
        URLWithString:[NSString
                          stringWithFormat:@"x-apple.systempreferences:com.apple.preference.security?Privacy_%s", tab]];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

void SetMacOSDarkMode(bool dark)
{
    if (dark) {
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    } else {
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    }
}

void TaskbarOverlayInit() {}

void TaskbarOverlaySetStatus(TaskbarOverlayStatus status)
{
    QIcon icon;
    if (status == TaskbarOverlayStatusActive)
        icon = QIcon::fromTheme("obs-active", QIcon(":/res/images/active_mac.png"));
    else if (status == TaskbarOverlayStatusPaused)
        icon = QIcon::fromTheme("obs-paused", QIcon(":/res/images/paused_mac.png"));

    NSDockTile *tile = [NSApp dockTile];
    [tile setContentView:[[DockView alloc] initWithIcon:icon]];
    [tile display];
}

/*
 * This custom NSApplication subclass makes the app compatible with CEF. Qt
 * also has an NSApplication subclass, but it doesn't conflict thanks to Qt
 * using arcane magic to hook into the NSApplication superclass itself if the
 * program has its own NSApplication subclass.
 */

@protocol CrAppProtocol
- (BOOL)isHandlingSendEvent;
@end

@interface OBSApplication : NSApplication <CrAppProtocol>
@property (nonatomic, getter=isHandlingSendEvent) BOOL handlingSendEvent;
@end

@implementation OBSApplication

- (void)sendEvent:(NSEvent *)event
{
    _handlingSendEvent = YES;
    [super sendEvent:event];
    _handlingSendEvent = NO;
}

@end

void InstallNSThreadLocks()
{
    [[NSThread new] start];

    if ([NSThread isMultiThreaded] != 1) {
        abort();
    }
}

void InstallNSApplicationSubclass()
{
    [OBSApplication sharedApplication];
}

bool HighContrastEnabled()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast];
}
