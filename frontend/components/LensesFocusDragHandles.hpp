#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;
class QToolButton;

namespace lenses::focus {

class DragHandles final : public QObject {
public:
	explicit DragHandles(QWidget *hostSurface, QWidget *dragWindow = nullptr);
	~DragHandles() override = default;

	void SetVisible(bool visible);
	void UpdateLayout();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void ShowAndArmAutoHide();
	void StartFadeOut();

	QPointer<QWidget> hostSurface_;
	QPointer<QWidget> dragWindow_;
	QPointer<QToolButton> rightButton_;
	QGraphicsOpacityEffect *opacityEffect_ = nullptr;
	QPropertyAnimation *opacityAnimation_ = nullptr;
	QTimer *idleHideTimer_ = nullptr;
	bool visible_ = false;
};

} // namespace lenses::focus
