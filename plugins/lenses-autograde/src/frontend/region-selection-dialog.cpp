#include "frontend/region-selection-dialog.hpp"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QtMath>

namespace lenses_autograde::frontend {

RegionSelectionCanvas::RegionSelectionCanvas(const QImage &image,
					     const std::optional<NormalizedRegion> &initial_region,
					     QWidget *parent)
	: QWidget(parent), image_(image.convertToFormat(QImage::Format_ARGB32))
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	if (!image_.isNull() && initial_region.has_value()) {
		const qreal x = qBound(0.0, (qreal)initial_region->x, 1.0) * (qreal)image_.width();
		const qreal y = qBound(0.0, (qreal)initial_region->y, 1.0) * (qreal)image_.height();
		const qreal w = qBound(0.0, (qreal)initial_region->width, 1.0) * (qreal)image_.width();
		const qreal h =
			qBound(0.0, (qreal)initial_region->height, 1.0) * (qreal)image_.height();
		selection_image_ = QRectF(x, y, w, h).normalized();
		NormalizeSelection();
	}
}

bool RegionSelectionCanvas::HasSelection() const
{
	return selection_image_.width() >= kMinSelectionPixels &&
	       selection_image_.height() >= kMinSelectionPixels;
}

std::optional<NormalizedRegion> RegionSelectionCanvas::GetNormalizedSelection() const
{
	if (!HasSelection() || image_.isNull())
		return std::nullopt;

	QRectF clamped = selection_image_.normalized();
	clamped.setLeft(qBound(0.0, clamped.left(), (qreal)image_.width()));
	clamped.setTop(qBound(0.0, clamped.top(), (qreal)image_.height()));
	clamped.setRight(qBound(0.0, clamped.right(), (qreal)image_.width()));
	clamped.setBottom(qBound(0.0, clamped.bottom(), (qreal)image_.height()));

	NormalizedRegion region;
	region.x = (float)(clamped.x() / (qreal)image_.width());
	region.y = (float)(clamped.y() / (qreal)image_.height());
	region.width = (float)(clamped.width() / (qreal)image_.width());
	region.height = (float)(clamped.height() / (qreal)image_.height());
	return region;
}

QRectF RegionSelectionCanvas::ImageRectInWidget() const
{
	if (image_.isNull())
		return QRectF();

	const QSizeF canvas_size = size();
	const QSizeF image_size = image_.size();
	if (canvas_size.width() <= 1.0 || canvas_size.height() <= 1.0 || image_size.width() <= 1.0 ||
	    image_size.height() <= 1.0) {
		return QRectF();
	}

	const qreal sx = canvas_size.width() / image_size.width();
	const qreal sy = canvas_size.height() / image_size.height();
	const qreal scale = qMin(sx, sy);
	const qreal draw_w = image_size.width() * scale;
	const qreal draw_h = image_size.height() * scale;
	const qreal draw_x = (canvas_size.width() - draw_w) * 0.5;
	const qreal draw_y = (canvas_size.height() - draw_h) * 0.5;
	return QRectF(draw_x, draw_y, draw_w, draw_h);
}

QPointF RegionSelectionCanvas::ClampToImage(const QPointF &point) const
{
	if (image_.isNull())
		return QPointF();

	return QPointF(qBound(0.0, point.x(), (qreal)image_.width()),
		      qBound(0.0, point.y(), (qreal)image_.height()));
}

QPointF RegionSelectionCanvas::WidgetToImageSpace(const QPointF &point) const
{
	if (image_draw_rect_.isEmpty() || image_.isNull())
		return QPointF();

	const qreal nx = (point.x() - image_draw_rect_.x()) / image_draw_rect_.width();
	const qreal ny = (point.y() - image_draw_rect_.y()) / image_draw_rect_.height();
	return ClampToImage(QPointF(nx * image_.width(), ny * image_.height()));
}

QPointF RegionSelectionCanvas::ImageToWidgetSpace(const QPointF &point) const
{
	if (image_draw_rect_.isEmpty() || image_.isNull())
		return QPointF();

	const qreal nx = point.x() / (qreal)image_.width();
	const qreal ny = point.y() / (qreal)image_.height();
	return QPointF(image_draw_rect_.x() + nx * image_draw_rect_.width(),
		      image_draw_rect_.y() + ny * image_draw_rect_.height());
}

QRectF RegionSelectionCanvas::SelectionInWidgetSpace() const
{
	if (!HasSelection())
		return QRectF();

	const QPointF p0 = ImageToWidgetSpace(selection_image_.topLeft());
	const QPointF p1 = ImageToWidgetSpace(selection_image_.bottomRight());
	return QRectF(p0, p1).normalized();
}

RegionSelectionCanvas::DragMode RegionSelectionCanvas::HitTest(const QPointF &widget_point) const
{
	if (!HasSelection())
		return DragMode::None;

	const QRectF sel = SelectionInWidgetSpace();
	const QPointF top_left = sel.topLeft();
	const QPointF top_right = sel.topRight();
	const QPointF bottom_left = sel.bottomLeft();
	const QPointF bottom_right = sel.bottomRight();

	auto near = [&](const QPointF &p) {
		return QLineF(widget_point, p).length() <= (kHandleRadius + 2.0);
	};
	if (near(top_left))
		return DragMode::ResizeNW;
	if (near(top_right))
		return DragMode::ResizeNE;
	if (near(bottom_left))
		return DragMode::ResizeSW;
	if (near(bottom_right))
		return DragMode::ResizeSE;

	const QRectF top_band(sel.x() + kHandleRadius, sel.y() - kHandleRadius, sel.width() -
			      2.0 * kHandleRadius,
			      2.0 * kHandleRadius);
	const QRectF bottom_band(sel.x() + kHandleRadius, sel.bottom() - kHandleRadius,
				 sel.width() - 2.0 * kHandleRadius, 2.0 * kHandleRadius);
	const QRectF left_band(sel.x() - kHandleRadius, sel.y() + kHandleRadius, 2.0 * kHandleRadius,
			    sel.height() - 2.0 * kHandleRadius);
	const QRectF right_band(sel.right() - kHandleRadius, sel.y() + kHandleRadius,
			     2.0 * kHandleRadius, sel.height() - 2.0 * kHandleRadius);

	if (top_band.contains(widget_point))
		return DragMode::ResizeN;
	if (bottom_band.contains(widget_point))
		return DragMode::ResizeS;
	if (left_band.contains(widget_point))
		return DragMode::ResizeW;
	if (right_band.contains(widget_point))
		return DragMode::ResizeE;

	if (sel.contains(widget_point))
		return DragMode::Moving;

	return DragMode::None;
}

void RegionSelectionCanvas::NormalizeSelection()
{
	selection_image_ = selection_image_.normalized();
	selection_image_.setLeft(qBound(0.0, selection_image_.left(), (qreal)image_.width()));
	selection_image_.setTop(qBound(0.0, selection_image_.top(), (qreal)image_.height()));
	selection_image_.setRight(qBound(0.0, selection_image_.right(), (qreal)image_.width()));
	selection_image_.setBottom(qBound(0.0, selection_image_.bottom(), (qreal)image_.height()));
}

void RegionSelectionCanvas::ApplyResize(const QPointF &image_point)
{
	QRectF rect = drag_original_selection_;
	const QPointF p = ClampToImage(image_point);

	switch (drag_mode_) {
	case DragMode::ResizeN:
		rect.setTop(p.y());
		break;
	case DragMode::ResizeS:
		rect.setBottom(p.y());
		break;
	case DragMode::ResizeE:
		rect.setRight(p.x());
		break;
	case DragMode::ResizeW:
		rect.setLeft(p.x());
		break;
	case DragMode::ResizeNE:
		rect.setTop(p.y());
		rect.setRight(p.x());
		break;
	case DragMode::ResizeNW:
		rect.setTop(p.y());
		rect.setLeft(p.x());
		break;
	case DragMode::ResizeSE:
		rect.setBottom(p.y());
		rect.setRight(p.x());
		break;
	case DragMode::ResizeSW:
		rect.setBottom(p.y());
		rect.setLeft(p.x());
		break;
	default:
		break;
	}

	rect = rect.normalized();
	if (rect.width() < kMinSelectionPixels)
		rect.setWidth(kMinSelectionPixels);
	if (rect.height() < kMinSelectionPixels)
		rect.setHeight(kMinSelectionPixels);
	if (rect.right() > image_.width())
		rect.moveRight((qreal)image_.width());
	if (rect.bottom() > image_.height())
		rect.moveBottom((qreal)image_.height());
	if (rect.left() < 0.0)
		rect.moveLeft(0.0);
	if (rect.top() < 0.0)
		rect.moveTop(0.0);

	selection_image_ = rect;
}

void RegionSelectionCanvas::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);

	image_draw_rect_ = ImageRectInWidget();
	QPainter painter(this);
	painter.fillRect(rect(), QColor(12, 12, 12));

	if (image_.isNull() || image_draw_rect_.isEmpty())
		return;

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.drawImage(image_draw_rect_, image_);

	const QRectF selection = SelectionInWidgetSpace();
	QPainterPath dimmed;
	dimmed.addRect(QRectF(rect()));
	if (!selection.isEmpty()) {
		QPainterPath selected;
		selected.addRect(selection);
		dimmed = dimmed.subtracted(selected);
	}
	painter.fillPath(dimmed, QColor(0, 0, 0, 120));

	if (!selection.isEmpty()) {
		QPen border(QColor(88, 190, 255), 2.0);
		painter.setPen(border);
		painter.drawRect(selection);
		painter.setBrush(QColor(88, 190, 255));

		const QPointF handles[] = {
			selection.topLeft(), selection.topRight(), selection.bottomLeft(), selection.bottomRight(),
		};
		for (const QPointF &handle : handles)
			painter.drawEllipse(handle, kHandleRadius, kHandleRadius);
	}

	painter.setPen(Qt::white);
	const QString hint = HasSelection() ? tr("Drag to select. Move/resize selection. Enter=Apply, Esc=Cancel")
					    : tr("Drag to create selection. Enter=Apply, Esc=Cancel");
	painter.drawText(QRectF(12.0, 10.0, width() - 24.0, 28.0),
			 Qt::AlignLeft | Qt::AlignVCenter, hint);
}

void RegionSelectionCanvas::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || image_draw_rect_.isEmpty()) {
		event->ignore();
		return;
	}

	const QPointF pos = event->position();
	if (!image_draw_rect_.contains(pos)) {
		event->ignore();
		return;
	}

	const DragMode hit = HitTest(pos);
	drag_anchor_image_ = WidgetToImageSpace(pos);
	drag_original_selection_ = selection_image_;

	if (hit == DragMode::None) {
		drag_mode_ = DragMode::Creating;
		selection_image_ = QRectF(drag_anchor_image_, drag_anchor_image_);
	} else {
		drag_mode_ = hit;
	}

	update();
}

void RegionSelectionCanvas::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF pos = event->position();
	if (drag_mode_ == DragMode::None) {
		switch (HitTest(pos)) {
		case DragMode::ResizeN:
		case DragMode::ResizeS:
			setCursor(Qt::SizeVerCursor);
			break;
		case DragMode::ResizeE:
		case DragMode::ResizeW:
			setCursor(Qt::SizeHorCursor);
			break;
		case DragMode::ResizeNE:
		case DragMode::ResizeSW:
			setCursor(Qt::SizeBDiagCursor);
			break;
		case DragMode::ResizeNW:
		case DragMode::ResizeSE:
			setCursor(Qt::SizeFDiagCursor);
			break;
		case DragMode::Moving:
			setCursor(Qt::SizeAllCursor);
			break;
		default:
			setCursor(Qt::CrossCursor);
			break;
		}
		return;
	}

	const QPointF image_point = WidgetToImageSpace(pos);
	switch (drag_mode_) {
	case DragMode::Creating:
		selection_image_ = QRectF(drag_anchor_image_, image_point).normalized();
		break;
	case DragMode::Moving: {
		QRectF moved = drag_original_selection_.translated(image_point - drag_anchor_image_);
		if (moved.left() < 0.0)
			moved.moveLeft(0.0);
		if (moved.top() < 0.0)
			moved.moveTop(0.0);
		if (moved.right() > image_.width())
			moved.moveRight((qreal)image_.width());
		if (moved.bottom() > image_.height())
			moved.moveBottom((qreal)image_.height());
		selection_image_ = moved;
		break;
	}
	default:
		ApplyResize(image_point);
		break;
	}

	NormalizeSelection();
	update();
}

void RegionSelectionCanvas::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		drag_mode_ = DragMode::None;
	NormalizeSelection();
	update();
}

void RegionSelectionCanvas::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	image_draw_rect_ = ImageRectInWidget();
}

RegionSelectionDialog::RegionSelectionDialog(const QImage &image,
					     const std::optional<NormalizedRegion> &initial_region,
					     QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Select Auto Grade Region"));
	setModal(true);
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setMinimumSize(640, 420);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	canvas_ = new RegionSelectionCanvas(image, initial_region, this);
	layout->addWidget(canvas_, 1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Region"));
	buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	if (QWidget *window = parent ? parent->window() : nullptr) {
		const QRect geometry = window->geometry();
		resize((int)(geometry.width() * 0.72), (int)(geometry.height() * 0.72));
	}
}

std::optional<NormalizedRegion> RegionSelectionDialog::Selection() const
{
	if (!canvas_)
		return std::nullopt;
	return canvas_->GetNormalizedSelection();
}

void RegionSelectionDialog::keyPressEvent(QKeyEvent *event)
{
	if (!event)
		return;
	if (event->key() == Qt::Key_Escape) {
		reject();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		accept();
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

} // namespace lenses_autograde::frontend
