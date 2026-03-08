#pragma once

#include <QDialog>
#include <QImage>
#include <QRectF>
#include <QWidget>

#include <optional>

namespace lenses_autograde::frontend {

struct NormalizedRegion {
	float x = 0.0f;
	float y = 0.0f;
	float width = 1.0f;
	float height = 1.0f;
};

class RegionSelectionCanvas final : public QWidget {
public:
	RegionSelectionCanvas(const QImage &image,
			      const std::optional<NormalizedRegion> &initial_region,
			      QWidget *parent = nullptr);

	bool HasSelection() const;
	std::optional<NormalizedRegion> GetNormalizedSelection() const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	enum class DragMode {
		None,
		Creating,
		Moving,
		ResizeN,
		ResizeS,
		ResizeE,
		ResizeW,
		ResizeNE,
		ResizeNW,
		ResizeSE,
		ResizeSW,
	};

	QRectF ImageRectInWidget() const;
	QPointF ClampToImage(const QPointF &point) const;
	QPointF WidgetToImageSpace(const QPointF &point) const;
	QPointF ImageToWidgetSpace(const QPointF &point) const;
	QRectF SelectionInWidgetSpace() const;
	DragMode HitTest(const QPointF &widget_point) const;
	void ApplyResize(const QPointF &image_point);
	void NormalizeSelection();

	QImage image_;
	QRectF image_draw_rect_;
	QRectF selection_image_;
	QPointF drag_anchor_image_;
	QRectF drag_original_selection_;
	DragMode drag_mode_ = DragMode::None;
	static constexpr qreal kHandleRadius = 6.0;
	static constexpr qreal kMinSelectionPixels = 8.0;
};

class RegionSelectionDialog final : public QDialog {
public:
	RegionSelectionDialog(const QImage &image,
			      const std::optional<NormalizedRegion> &initial_region = std::nullopt,
			      QWidget *parent = nullptr);

	std::optional<NormalizedRegion> Selection() const;

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	RegionSelectionCanvas *canvas_ = nullptr;
};

} // namespace lenses_autograde::frontend
