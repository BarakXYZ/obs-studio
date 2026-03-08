#pragma once

#include "OBSQTDisplay.hpp"

#include <cstdint>

#include <obs.hpp>

class QScreen;
class QTimer;

namespace lenses::mirror {

class ExternalMirrorWindow final : public OBSQTDisplay {
public:
	explicit ExternalMirrorWindow(QWidget *parent = nullptr);
	~ExternalMirrorWindow() override;

	void SetRenderSource(obs_source_t *source);
	void SetTargetScreen(QScreen *screen);
	uint64_t GetCaptureExclusionId() const;
	void SetClickAssistanceEnabled(bool enabled);

private:
	static void OBSRender(void *data, uint32_t cx, uint32_t cy);
#ifdef __APPLE__
	void InitClickRoutingDiagnostics();
	void PollClickRoutingDiagnostics();
	void UpdateNativeClickThroughMode();
#endif

	OBSWeakSourceAutoRelease weakRenderSource_;
	bool ready_ = false;
#ifdef __APPLE__
	QTimer *clickRoutePollTimer_ = nullptr;
	bool primaryButtonWasDown_ = false;
	bool activationAssistInFlight_ = false;
	bool nonActivatingPanelApplied_ = false;
	bool clickThroughModeLogged_ = false;
	bool clickAssistanceEnabled_ = true;
	bool lastLoggedClickAssistanceEnabled_ = true;
	int lastObservedFrontmostPid_ = 0;
	int64_t suppressAssistUntilMs_ = 0;
	uint64_t lastLoggedWinnerWindowId_ = 0;
	bool lastLoggedActivationLikely_ = false;
#endif
};

} // namespace lenses::mirror
