#include "LensesFocusDragHandles.hpp"

#include <algorithm>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace {

class DragButton final : public QToolButton {
public:
	explicit DragButton(QWidget *parent, QWidget *dragWindow) : QToolButton(parent), dragWindow_(dragWindow)
	{
		setObjectName("lensesFocusDragButton");
		setCursor(Qt::SizeAllCursor);
		setFocusPolicy(Qt::NoFocus);
		setAttribute(Qt::WA_NativeWindow);
		setFixedSize(24, 24);
		setStyleSheet(
			"QToolButton#lensesFocusDragButton {"
			"  background-color: rgba(18, 18, 18, 120);"
			"  border: 1px solid rgba(255, 255, 255, 48);"
			"  border-radius: 12px;"
			"}"
			"QToolButton#lensesFocusDragButton:hover {"
			"  background-color: rgba(18, 18, 18, 170);"
			"}"
			"QToolButton#lensesFocusDragButton:pressed {"
			"  background-color: rgba(18, 18, 18, 210);"
			"}");
		setIcon(CreateGripIcon(palette().buttonText().color()));
		setIconSize(QSize(12, 12));
		setToolTip("Drag Window");
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton) {
			QToolButton::mousePressEvent(event);
			return;
		}

		QWidget *hostWindow = dragWindow_ ? dragWindow_ : window();
		if (!hostWindow) {
			QToolButton::mousePressEvent(event);
			return;
		}

		QWindow *nativeWindow = hostWindow->windowHandle();
		if (nativeWindow && nativeWindow->startSystemMove()) {
			event->accept();
			return;
		}

		manualDrag_ = true;
		dragOffset_ = GlobalPoint(event) - hostWindow->frameGeometry().topLeft();
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!manualDrag_ || !(event->buttons() & Qt::LeftButton)) {
			QToolButton::mouseMoveEvent(event);
			return;
		}

		QWidget *hostWindow = dragWindow_ ? dragWindow_ : window();
		if (!hostWindow || hostWindow->isFullScreen()) {
			event->accept();
			return;
		}

		if (!hostWindow->isMaximized())
			hostWindow->move(GlobalPoint(event) - dragOffset_);

		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		manualDrag_ = false;
		QToolButton::mouseReleaseEvent(event);
	}

private:
	QWidget *dragWindow_ = nullptr;
	bool manualDrag_ = false;
	QPoint dragOffset_;

	static QPoint GlobalPoint(const QMouseEvent *event)
	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		return event->globalPosition().toPoint();
#else
		return event->globalPos();
#endif
	}

	static QIcon CreateGripIcon(const QColor &color)
	{
		QPixmap pixmap(12, 12);
		pixmap.fill(Qt::transparent);

		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);

		const int radius = 1;
		const int step = 4;
		const int startX = 2;
		const int startY = 3;
		for (int row = 0; row < 2; ++row) {
			for (int col = 0; col < 3; ++col) {
				const QPoint center(startX + col * step, startY + row * step);
				painter.drawEllipse(center, radius, radius);
			}
		}

		return QIcon(pixmap);
	}
};

constexpr int kHandleSize = 24;
constexpr int kSideMargin = 8;
constexpr int kTopMargin = 8;
constexpr int kFadeDurationMs = 180;
constexpr int kIdleHideDelayMs = 1500;
constexpr int kLeaveHideDelayMs = 220;

} // namespace

namespace lenses::focus {

DragHandles::DragHandles(QWidget *hostSurface, QWidget *dragWindow)
	: hostSurface_(hostSurface), dragWindow_(dragWindow ? dragWindow : (hostSurface ? hostSurface->window() : nullptr))
{
	if (!hostSurface_)
		return;

	rightButton_ = new DragButton(hostSurface_, dragWindow_);
	rightButton_->hide();
	rightButton_->setMouseTracking(true);
	rightButton_->installEventFilter(this);

	hostSurface_->setMouseTracking(true);
	hostSurface_->installEventFilter(this);

	opacityEffect_ = new QGraphicsOpacityEffect(rightButton_);
	opacityEffect_->setOpacity(1.0);
	rightButton_->setGraphicsEffect(opacityEffect_);

	opacityAnimation_ = new QPropertyAnimation(opacityEffect_, "opacity", this);
	opacityAnimation_->setDuration(kFadeDurationMs);
	opacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);
	connect(opacityAnimation_, &QPropertyAnimation::finished, this, [this]() {
		if (!rightButton_ || !opacityEffect_)
			return;
		if (opacityEffect_->opacity() <= 0.01)
			rightButton_->hide();
	});

	idleHideTimer_ = new QTimer(this);
	idleHideTimer_->setSingleShot(true);
	idleHideTimer_->setInterval(kIdleHideDelayMs);
	connect(idleHideTimer_, &QTimer::timeout, this, [this]() { StartFadeOut(); });
}

void DragHandles::SetVisible(bool visible)
{
	visible_ = visible;
	if (!rightButton_)
		return;

	if (!visible_) {
		if (idleHideTimer_)
			idleHideTimer_->stop();
		if (opacityAnimation_)
			opacityAnimation_->stop();
		rightButton_->hide();
		return;
	}

	ShowAndArmAutoHide();
	UpdateLayout();
}

void DragHandles::UpdateLayout()
{
	if (!hostSurface_ || !rightButton_)
		return;

	if (!visible_) {
		rightButton_->hide();
		return;
	}

	const int rightX = std::max(kSideMargin, hostSurface_->width() - kSideMargin - kHandleSize);
	rightButton_->setGeometry(rightX, kTopMargin, kHandleSize, kHandleSize);
	rightButton_->raise();
}

void DragHandles::ShowAndArmAutoHide()
{
	if (!visible_ || !rightButton_ || !opacityEffect_)
		return;

	if (opacityAnimation_)
		opacityAnimation_->stop();

	opacityEffect_->setOpacity(1.0);
	rightButton_->show();
	rightButton_->raise();

	if (idleHideTimer_)
		idleHideTimer_->start(kIdleHideDelayMs);
}

void DragHandles::StartFadeOut()
{
	if (!visible_ || !rightButton_ || !opacityEffect_ || !opacityAnimation_)
		return;
	if (rightButton_->isDown() || rightButton_->underMouse()) {
		if (idleHideTimer_)
			idleHideTimer_->start(kIdleHideDelayMs);
		return;
	}
	if (!rightButton_->isVisible())
		return;

	opacityAnimation_->stop();
	opacityAnimation_->setStartValue(opacityEffect_->opacity());
	opacityAnimation_->setEndValue(0.0);
	opacityAnimation_->start();
}

bool DragHandles::eventFilter(QObject *watched, QEvent *event)
{
	if (!visible_)
		return QObject::eventFilter(watched, event);

	if (watched == hostSurface_) {
		switch (event->type()) {
		case QEvent::MouseMove:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::Enter:
		case QEvent::HoverMove:
		case QEvent::HoverEnter:
		case QEvent::Wheel:
			ShowAndArmAutoHide();
			break;
		case QEvent::Leave:
		case QEvent::HoverLeave:
			if (idleHideTimer_)
				idleHideTimer_->start(kLeaveHideDelayMs);
			break;
		case QEvent::Resize:
		case QEvent::Move:
		case QEvent::Show:
			UpdateLayout();
			break;
		default:
			break;
		}
	}

	if (watched == rightButton_) {
		switch (event->type()) {
		case QEvent::MouseMove:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::Enter:
		case QEvent::HoverMove:
		case QEvent::HoverEnter:
			ShowAndArmAutoHide();
			break;
		case QEvent::Leave:
		case QEvent::HoverLeave:
			if (idleHideTimer_)
				idleHideTimer_->start(kIdleHideDelayMs);
			break;
		default:
			break;
		}
	}

	return QObject::eventFilter(watched, event);
}

} // namespace lenses::focus
