#include "LensesMirrorWindow.hpp"

#include <components/Multiview.hpp>
#include <utility/platform.hpp>

#include <algorithm>

#include <QDateTime>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <functional>
#include <memory>

namespace lenses::mirror {

#ifdef __APPLE__
namespace {
constexpr int kClickPollIntervalMs = 16;
constexpr int kRetryIntervalMs = 8;
constexpr int kMaxMouseReleaseAttempts = 90;
constexpr int kMaxActivationAttempts = 60;
constexpr int64_t kSuppressAssistMs = 180;
}
#endif

ExternalMirrorWindow::ExternalMirrorWindow(QWidget *parent)
	: OBSQTDisplay(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
				       Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput)
{
	setAttribute(Qt::WA_DeleteOnClose, false);
	setAttribute(Qt::WA_QuitOnClose, false);
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
#ifdef __APPLE__
	setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
	setFocusPolicy(Qt::NoFocus);
	setWindowTitle("Lenses Mirror");

	connect(this, &OBSQTDisplay::DisplayCreated, this, [this]() {
		if (!GetDisplay())
			return;
		obs_display_add_draw_callback(GetDisplay(), OBSRender, this);
		obs_display_set_background_color(GetDisplay(), 0x000000);
	});

	ready_ = true;
	show();
#ifdef __APPLE__
	UpdateNativeClickThroughMode();
#endif
	SetWindowCaptureExcluded(this, true);
	SetWindowInputPassthrough(this, true);
}

ExternalMirrorWindow::~ExternalMirrorWindow()
{
	if (GetDisplay())
		obs_display_remove_draw_callback(GetDisplay(), OBSRender, this);
}

void ExternalMirrorWindow::SetRenderSource(obs_source_t *source)
{
	weakRenderSource_ = OBSGetWeakRef(source);
}

void ExternalMirrorWindow::SetTargetScreen(QScreen *screen)
{
	if (!screen)
		return;

	if (windowHandle())
		windowHandle()->setScreen(screen);

	setGeometry(screen->geometry());
	show();
	raise();
#ifdef __APPLE__
	UpdateNativeClickThroughMode();
#endif
	SetWindowCaptureExcluded(this, true);
	SetWindowInputPassthrough(this, true);
}

uint64_t ExternalMirrorWindow::GetCaptureExclusionId() const
{
	return GetWindowCaptureExclusionId(const_cast<ExternalMirrorWindow *>(this));
}

void ExternalMirrorWindow::SetClickAssistanceEnabled(bool enabled)
{
#ifdef __APPLE__
	if (clickAssistanceEnabled_ == enabled)
		return;

	clickAssistanceEnabled_ = enabled;
	UpdateNativeClickThroughMode();
#else
	(void)enabled;
#endif
}

void ExternalMirrorWindow::OBSRender(void *data, uint32_t cx, uint32_t cy)
{
	ExternalMirrorWindow *window = static_cast<ExternalMirrorWindow *>(data);
	if (!window || !window->ready_)
		return;

	OBSSource source = OBSGetStrongRef(window->weakRenderSource_);
	if (!source)
		return;

	const uint32_t targetCX = std::max(obs_source_get_width(source), 1u);
	const uint32_t targetCY = std::max(obs_source_get_height(source), 1u);
	if (cx == 0 || cy == 0)
		return;

	startRegion(0, 0, int(cx), int(cy), 0.0f, float(targetCX), 0.0f, float(targetCY));
	obs_source_video_render(source);
	endRegion();
}

#ifdef __APPLE__
void ExternalMirrorWindow::InitClickRoutingDiagnostics()
{
	if (clickRoutePollTimer_)
		return;

	clickRoutePollTimer_ = new QTimer(this);
	clickRoutePollTimer_->setInterval(kClickPollIntervalMs);
	connect(clickRoutePollTimer_, &QTimer::timeout, this, &ExternalMirrorWindow::PollClickRoutingDiagnostics);
	clickRoutePollTimer_->start();
}

void ExternalMirrorWindow::UpdateNativeClickThroughMode()
{
	const bool previousMode = nonActivatingPanelApplied_;
	nonActivatingPanelApplied_ = SetWindowNonActivatingPanel(this, true);
	if (!clickThroughModeLogged_ || previousMode != nonActivatingPanelApplied_ ||
	    lastLoggedClickAssistanceEnabled_ != clickAssistanceEnabled_) {
		blog(LOG_INFO,
		     "[lenses] mirror click-through mode nonactivating_panel=%d click_assist=%d",
		     (int)nonActivatingPanelApplied_, (int)clickAssistanceEnabled_);
		clickThroughModeLogged_ = true;
		lastLoggedClickAssistanceEnabled_ = clickAssistanceEnabled_;
	}

	if (!clickAssistanceEnabled_) {
		if (clickRoutePollTimer_)
			clickRoutePollTimer_->stop();
		return;
	}

	if (!clickRoutePollTimer_)
		InitClickRoutingDiagnostics();
	else if (!clickRoutePollTimer_->isActive())
		clickRoutePollTimer_->start();
}

void ExternalMirrorWindow::PollClickRoutingDiagnostics()
{
	const bool isDown = IsPrimaryMouseButtonDown();
	const int frontmostPidNow = GetFrontmostProcessId();
	const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
	if (!isDown)
		lastObservedFrontmostPid_ = frontmostPidNow;

	if (nowMs < suppressAssistUntilMs_) {
		primaryButtonWasDown_ = isDown;
		return;
	}

	if (!clickAssistanceEnabled_) {
		primaryButtonWasDown_ = isDown;
		return;
	}

	if (!isVisible() || !isDown || primaryButtonWasDown_) {
		primaryButtonWasDown_ = isDown;
		return;
	}

	WindowHitInfo hitInfo{};
	const uint64_t mirrorWindowId = GetCaptureExclusionId();
	if (QueryWindowHitInfoAtMouseLocation(mirrorWindowId, hitInfo)) {
		const int frontmostPidBeforeDown = lastObservedFrontmostPid_;
		const bool ownerWasNotFrontmostBeforeDown =
			frontmostPidBeforeDown > 0 && hitInfo.ownerProcessId > 0 && frontmostPidBeforeDown != hitInfo.ownerProcessId;
		const bool ownerStillNotFrontmostNow = hitInfo.ownerProcessId <= 0 || frontmostPidNow != hitInfo.ownerProcessId;
		const bool activationOnlyLikely =
			!hitInfo.ownerIsCurrentProcess &&
			((ownerWasNotFrontmostBeforeDown && ownerStillNotFrontmostNow) ||
			 (frontmostPidBeforeDown <= 0 && !hitInfo.ownerIsFrontmostApplication));

		const bool winnerChanged = hitInfo.windowId != lastLoggedWinnerWindowId_;
		const bool likelihoodChanged = activationOnlyLikely != lastLoggedActivationLikely_;
		if (activationOnlyLikely || winnerChanged || likelihoodChanged) {
			blog(LOG_DEBUG,
			     "[lenses] click winner window=%llu owner=\"%s\" title=\"%s\" owner_is_obs=%d "
			     "owner_is_frontmost=%d owner_pid=%d frontmost_before_pid=%d frontmost_now_pid=%d "
			     "activation_only_likely=%d",
			     (unsigned long long)hitInfo.windowId, hitInfo.ownerName, hitInfo.windowTitle,
			     (int)hitInfo.ownerIsCurrentProcess, (int)hitInfo.ownerIsFrontmostApplication,
			     hitInfo.ownerProcessId, frontmostPidBeforeDown, frontmostPidNow, (int)activationOnlyLikely);
		}
		lastLoggedWinnerWindowId_ = hitInfo.windowId;
		lastLoggedActivationLikely_ = activationOnlyLikely;

		if (activationOnlyLikely && !activationAssistInFlight_ && hitInfo.ownerProcessId > 0) {
			activationAssistInFlight_ = true;
			const int targetPid = hitInfo.ownerProcessId;
			const double clickX = hitInfo.globalMouseX;
			const double clickY = hitInfo.globalMouseY;
			DeactivateCurrentApplication();
			ActivateWindowOwnerApplication(hitInfo);

			auto retry = std::make_shared<std::function<void(int)>>();
			*retry = [this, retry, hitInfo, targetPid, clickX, clickY](int attempt) {
				const bool mouseReleased = !IsPrimaryMouseButtonDown();
				const int frontmostPid = GetFrontmostProcessId();
				const bool targetFrontmost = targetPid > 0 && frontmostPid == targetPid;

				if (!mouseReleased) {
					if (attempt >= kMaxMouseReleaseAttempts) {
						blog(LOG_WARNING,
						     "[lenses] activation assist timed out waiting_mouse_release target_pid=%d",
						     targetPid);
						activationAssistInFlight_ = false;
						return;
					}

					QTimer::singleShot(kRetryIntervalMs, this, [retry, attempt]() { (*retry)(attempt + 1); });
					return;
				}

				// If target app is still not frontmost, keep trying activation first.
				if (!targetFrontmost && attempt < kMaxActivationAttempts) {
					DeactivateCurrentApplication();
					ActivateWindowOwnerApplication(hitInfo);
					QTimer::singleShot(kRetryIntervalMs, this, [retry, attempt]() { (*retry)(attempt + 1); });
					return;
				}

				const bool pressed = targetFrontmost ? PerformAccessibilityPressAt(clickX, clickY, targetPid) : false;
				const bool replayed = pressed ? false : ReplayPrimaryClickAt(clickX, clickY, targetPid);
				suppressAssistUntilMs_ = QDateTime::currentMSecsSinceEpoch() + kSuppressAssistMs;
				blog(LOG_INFO,
				     "[lenses] activation assist ax_pressed=%d replayed_click=%d target_pid=%d "
				     "frontmost_pid=%d x=%.1f y=%.1f wait_attempts=%d",
				     (int)pressed, (int)replayed, targetPid, frontmostPid, clickX, clickY, attempt);
				activationAssistInFlight_ = false;
			};

			QTimer::singleShot(kRetryIntervalMs, this, [retry]() { (*retry)(0); });
		}
	} else {
		if (lastLoggedWinnerWindowId_ != 0) {
			blog(LOG_DEBUG, "[lenses] click winner window=<none>");
			lastLoggedWinnerWindowId_ = 0;
		}
	}

	lastObservedFrontmostPid_ = frontmostPidNow;
	primaryButtonWasDown_ = isDown;
}
#endif

} // namespace lenses::mirror
