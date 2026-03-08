#include "filter/ui/lenses-hue-qualifier-editor.h"

#include "filter/host/lenses-filter-hue-presets.h"
#include "filter/host/lenses-filter-hue-qualifier.h"
#include "filter/host/lenses-filter-internal.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace {

constexpr const char *k_hue_range_label_keys[LENSES_INVERT_HUE_RANGE_MAX_BANDS] = {
	"LensesFilter.InvertHueRange1", "LensesFilter.InvertHueRange2", "LensesFilter.InvertHueRange3",
	"LensesFilter.InvertHueRange4", "LensesFilter.InvertHueRange5", "LensesFilter.InvertHueRange6",
};

float clampf(float value, float min_value, float max_value)
{
	return std::min(std::max(value, min_value), max_value);
}

float normalize_degrees(float degrees)
{
	float normalized = std::fmod(degrees, 360.0f);
	if (normalized < 0.0f)
		normalized += 360.0f;
	return normalized;
}

float circular_distance_degrees(float lhs, float rhs)
{
	const float delta = std::fabs(normalize_degrees(lhs) - normalize_degrees(rhs));
	return std::min(delta, 360.0f - delta);
}

float band_membership(float hue_degrees, const struct lenses_invert_hue_range_band &band)
{
	if (!band.enabled)
		return 0.0f;

	const float width = clampf(band.width_degrees, LENSES_INVERT_HUE_RANGE_WIDTH_MIN,
				   LENSES_INVERT_HUE_RANGE_WIDTH_MAX);
	const float softness = clampf(band.softness_degrees, LENSES_INVERT_HUE_RANGE_SOFTNESS_MIN,
			      LENSES_INVERT_HUE_RANGE_SOFTNESS_MAX);

	if (width >= 359.5f)
		return 1.0f;

	const float half_width = width * 0.5f;
	const float outer_half = half_width + softness;
	if (outer_half <= 0.0f)
		return 0.0f;

	const float distance = circular_distance_degrees(hue_degrees, band.center_degrees);
	if (distance <= half_width)
		return 1.0f;
	if (softness <= 0.0f || distance >= outer_half)
		return 0.0f;

	const float feather_t = (distance - half_width) / softness;
	return 1.0f - clampf(feather_t, 0.0f, 1.0f);
}

QColor hue_color(float degrees, int alpha)
{
	const float hue = normalize_degrees(degrees) / 360.0f;
	return QColor::fromHsvF(hue, 0.95, 0.95, clampf((float)alpha / 255.0f, 0.0f, 1.0f));
}

int first_enabled_band(const struct lenses_invert_hue_range_config &config)
{
	for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
		if (config.bands[i].enabled)
			return (int)i;
	}
	return 0;
}

const struct lenses_hue_preset_definition *preset_definition(enum lenses_hue_preset_id id)
{
	for (size_t i = 0; i < lenses_hue_preset_count(); ++i) {
		const struct lenses_hue_preset_definition *preset = lenses_hue_preset_at(i);
		if (preset && preset->id == id)
			return preset;
	}
	return nullptr;
}

class HueSpectrumWidget final : public QWidget {
public:
	explicit HueSpectrumWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(96);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setMouseTracking(true);
	}

	void SetConfig(const struct lenses_invert_hue_range_config &config)
	{
		config_ = config;
		update();
	}

	void SetSelectedBand(int band_index)
	{
		selected_band_ = std::clamp(band_index, 0, (int)LENSES_INVERT_HUE_RANGE_MAX_BANDS - 1);
		update();
	}

	void SetBandSelectedCallback(std::function<void(int)> callback)
	{
		on_band_selected_ = std::move(callback);
	}

	void SetCenterChangedCallback(std::function<void(float)> callback)
	{
		on_center_changed_ = std::move(callback);
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		UNUSED_PARAMETER(event);

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);

		const QRectF bar = BarRect();
		DrawHueBar(painter, bar);

		for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i)
			DrawBandOverlay(painter, bar, config_.bands[i], (int)i == selected_band_);

		QPen border(QColor(30, 30, 30, 210));
		border.setWidth(1);
		painter.setPen(border);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(bar, 6.0, 6.0);
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton)
			return;

		if (!BarRect().contains(event->position()))
			return;

		const float hue = PositionToHue(event->position().x());
		const int nearest = NearestEnabledBand(hue);
		if (nearest >= 0) {
			selected_band_ = nearest;
			if (on_band_selected_)
				on_band_selected_(selected_band_);
		}

		dragging_center_ = IsBandEditable(selected_band_);
		if (dragging_center_ && on_center_changed_)
			on_center_changed_(hue);
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!dragging_center_ || !(event->buttons() & Qt::LeftButton) || !on_center_changed_)
			return;

		on_center_changed_(PositionToHue(event->position().x()));
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		UNUSED_PARAMETER(event);
		dragging_center_ = false;
	}

private:
	QRectF BarRect() const
	{
		const qreal margin = 10.0;
		const qreal top = 14.0;
		const qreal height = std::max<qreal>(40.0, this->height() - 28.0);
		return QRectF(margin, top, std::max<qreal>(40.0, this->width() - margin * 2.0), height);
	}

	void DrawHueBar(QPainter &painter, const QRectF &bar)
	{
		QLinearGradient gradient(bar.topLeft(), bar.topRight());
		for (int i = 0; i <= 6; ++i) {
			const float t = (float)i / 6.0f;
			const float hue = (t >= 1.0f) ? 0.0f : t;
			gradient.setColorAt(t, QColor::fromHsvF(hue, 1.0, 1.0));
		}

		painter.setPen(Qt::NoPen);
		painter.setBrush(gradient);
		painter.drawRoundedRect(bar, 6.0, 6.0);
	}

	void DrawBandOverlay(QPainter &painter, const QRectF &bar,
			     const struct lenses_invert_hue_range_band &band, bool selected)
	{
		if (!band.enabled)
			return;

		const int width_px = std::max(1, (int)std::round(bar.width()));
		const QColor band_color = hue_color(band.center_degrees, selected ? 210 : 170);

		for (int x = 0; x < width_px; ++x) {
			const float hue = ((float)x / (float)(width_px - 1)) * 360.0f;
			const float membership = band_membership(hue, band);
			if (membership <= 0.001f)
				continue;

			QColor line_color = band_color;
			const float alpha_scale = selected ? 0.55f : 0.35f;
			line_color.setAlpha(std::clamp((int)std::round(255.0f * membership * alpha_scale), 0, 255));
			painter.setPen(line_color);
			const qreal px = bar.left() + (qreal)x;
			painter.drawLine(QPointF(px, bar.top()), QPointF(px, bar.bottom()));
		}

		DrawHandleLine(painter, bar, band.center_degrees,
			       selected ? QColor(255, 255, 255, 250) : QColor(20, 20, 20, 200),
			       selected ? 2.0 : 1.0);

		const float half_width = clampf(band.width_degrees, LENSES_INVERT_HUE_RANGE_WIDTH_MIN,
					LENSES_INVERT_HUE_RANGE_WIDTH_MAX) *
				 0.5f;
		DrawHandleLine(painter, bar, band.center_degrees - half_width,
			       selected ? QColor(255, 255, 255, 190) : QColor(30, 30, 30, 160), 1.0);
		DrawHandleLine(painter, bar, band.center_degrees + half_width,
			       selected ? QColor(255, 255, 255, 190) : QColor(30, 30, 30, 160), 1.0);
	}

	void DrawHandleLine(QPainter &painter, const QRectF &bar, float hue_degrees, const QColor &color,
		    qreal width)
	{
		QPen pen(color);
		pen.setWidthF(width);
		painter.setPen(pen);
		const qreal x = HueToPosition(hue_degrees, bar);
		painter.drawLine(QPointF(x, bar.top()), QPointF(x, bar.bottom()));
	}

	qreal HueToPosition(float hue_degrees, const QRectF &bar) const
	{
		const float norm = normalize_degrees(hue_degrees) / 360.0f;
		return bar.left() + bar.width() * norm;
	}

	float PositionToHue(qreal x) const
	{
		const QRectF bar = BarRect();
		const qreal clamped = std::clamp(x, bar.left(), bar.right());
		const qreal width = std::max<qreal>(1.0, bar.width());
		const float t = (float)((clamped - bar.left()) / width);
		return normalize_degrees(t * 360.0f);
	}

	bool IsBandEditable(int band_index) const
	{
		if (band_index < 0 || band_index >= (int)LENSES_INVERT_HUE_RANGE_MAX_BANDS)
			return false;
		return config_.bands[(size_t)band_index].enabled;
	}

	int NearestEnabledBand(float hue_degrees) const
	{
		float best_distance = 9999.0f;
		int best_index = -1;
		for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
			if (!config_.bands[i].enabled)
				continue;

			const float distance = circular_distance_degrees(hue_degrees, config_.bands[i].center_degrees);
			if (distance < best_distance) {
				best_distance = distance;
				best_index = (int)i;
			}
		}
		return best_index;
	}

	struct lenses_invert_hue_range_config config_ = {};
	int selected_band_ = 0;
	bool dragging_center_ = false;
	std::function<void(int)> on_band_selected_;
	std::function<void(float)> on_center_changed_;
};

class HueQualifierEditorDialog final : public QDialog {
public:
	HueQualifierEditorDialog(bool qualifier_enabled, int mode,
				 const struct lenses_invert_hue_range_config &config, QWidget *parent)
		: QDialog(parent), qualifier_enabled_(qualifier_enabled), mode_(mode), config_(config)
	{
		setWindowTitle(obs_module_text("LensesFilter.InvertHueEditorTitle"));
		setModal(true);
		resize(920, 520);
		selected_band_ = first_enabled_band(config_);

		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(14, 14, 14, 14);
		root->setSpacing(10);

		auto *hint = new QLabel(obs_module_text("LensesFilter.InvertHueEditorInfo"));
		hint->setWordWrap(true);
		root->addWidget(hint);

		auto *header = new QHBoxLayout();
		enabled_checkbox_ = new QCheckBox(obs_module_text("LensesFilter.InvertHueQualifierEnabled"));
		enabled_checkbox_->setChecked(qualifier_enabled_);
		header->addWidget(enabled_checkbox_);

		header->addSpacing(14);
		header->addWidget(new QLabel(obs_module_text("LensesFilter.InvertHueQualifierMode")));
		mode_combo_ = new QComboBox();
		mode_combo_->addItem(obs_module_text("LensesFilter.InvertHueQualifierMode.Exclude"),
				    LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE);
		mode_combo_->addItem(obs_module_text("LensesFilter.InvertHueQualifierMode.Include"),
				    LENSES_INVERT_HUE_RANGE_MODE_INCLUDE);
		const int mode_idx = mode_combo_->findData(mode_);
		if (mode_idx >= 0)
			mode_combo_->setCurrentIndex(mode_idx);
		header->addWidget(mode_combo_);
		header->addStretch(1);
		root->addLayout(header);

		auto *preset_group = new QGroupBox(obs_module_text("LensesFilter.InvertHueEditorPresets"));
		auto *preset_layout = new QGridLayout(preset_group);
		preset_layout->setContentsMargins(10, 10, 10, 10);
		preset_layout->setHorizontalSpacing(8);
		preset_layout->setVerticalSpacing(6);
		preset_layout->addWidget(new QLabel(obs_module_text("LensesFilter.InvertHueEditorPresetPicker")), 0,
					 0);
		preset_combo_ = new QComboBox();
		preset_combo_->addItem(obs_module_text("LensesFilter.InvertHuePreset.Custom"),
				       (int)LENSES_HUE_PRESET_CUSTOM);
		for (size_t i = 0; i < lenses_hue_preset_count(); ++i) {
			const struct lenses_hue_preset_definition *preset = lenses_hue_preset_at(i);
			if (!preset)
				continue;
			preset_combo_->addItem(obs_module_text(preset->label_key), (int)preset->id);
		}
		preset_layout->addWidget(preset_combo_, 0, 1);
		preset_apply_button_ = new QPushButton(obs_module_text("LensesFilter.InvertHuePresetApply"));
		preset_layout->addWidget(preset_apply_button_, 0, 2);
		preset_description_label_ = new QLabel();
		preset_description_label_->setWordWrap(true);
		preset_layout->addWidget(preset_description_label_, 1, 0, 1, 3);
		root->addWidget(preset_group);

		spectrum_ = new HueSpectrumWidget();
		spectrum_->SetConfig(config_);
		root->addWidget(spectrum_);

		auto *bands_group = new QGroupBox(obs_module_text("LensesFilter.InvertHueEditorRanges"));
		auto *bands_layout = new QGridLayout(bands_group);
		bands_layout->setContentsMargins(10, 10, 10, 10);
		bands_layout->setHorizontalSpacing(8);
		bands_layout->setVerticalSpacing(6);

		bands_layout->addWidget(new QLabel(obs_module_text("LensesFilter.InvertHueEditorSlot")), 0, 0);
		bands_layout->addWidget(new QLabel(obs_module_text("LensesFilter.InvertHueRangeEnabled")), 0, 1);
		bands_layout->addWidget(new QLabel(obs_module_text("LensesFilter.InvertHueEditorSummary")), 0, 2);

		band_group_ = new QButtonGroup(this);
		band_group_->setExclusive(true);

		for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
			auto *radio = new QRadioButton(obs_module_text(k_hue_range_label_keys[i]));
			band_group_->addButton(radio, (int)i);
			bands_layout->addWidget(radio, (int)i + 1, 0);

			auto *enabled = new QCheckBox();
			enabled->setChecked(config_.bands[i].enabled);
			bands_layout->addWidget(enabled, (int)i + 1, 1, Qt::AlignHCenter);
			band_enabled_checks_[i] = enabled;

			auto *summary = new QLabel();
			summary->setMinimumWidth(280);
			band_summary_labels_[i] = summary;
			bands_layout->addWidget(summary, (int)i + 1, 2);

			QObject::connect(enabled, &QCheckBox::toggled, this, [this, i](bool checked) {
				config_.bands[i].enabled = checked;
				if (checked) {
					selected_band_ = (int)i;
					if (QAbstractButton *button = band_group_->button((int)i))
						button->setChecked(true);
				}
				RefreshBandRows();
				SyncSpectrum();
				SyncBandControls();
				SyncPresetSelection();
			});
		}

		if (QAbstractButton *first = band_group_->button(selected_band_))
			first->setChecked(true);

		QObject::connect(band_group_, &QButtonGroup::idClicked, this, [this](int id) {
			selected_band_ = std::clamp(id, 0, (int)LENSES_INVERT_HUE_RANGE_MAX_BANDS - 1);
			SyncBandControls();
			SyncSpectrum();
		});

		root->addWidget(bands_group);

		auto *control_group = new QGroupBox(obs_module_text("LensesFilter.InvertHueEditorBandControls"));
		auto *control_layout = new QGridLayout(control_group);
		control_layout->setContentsMargins(10, 10, 10, 10);
		control_layout->setHorizontalSpacing(8);
		control_layout->setVerticalSpacing(8);

		AddBandControlRow(control_layout, 0, obs_module_text("LensesFilter.InvertHueRangeCenterDegrees"),
				  0, 360, center_slider_, center_spin_);
		AddBandControlRow(control_layout, 1, obs_module_text("LensesFilter.InvertHueRangeWidthDegrees"),
				  0, 360, width_slider_, width_spin_);
		AddBandControlRow(control_layout, 2, obs_module_text("LensesFilter.InvertHueRangeSoftnessDegrees"),
				  0, 180, softness_slider_, softness_spin_);

		control_layout->addWidget(
			new QLabel(obs_module_text("LensesFilter.InvertHueEditorQuickTemplates")), 3, 0);
		auto *template_actions = new QHBoxLayout();
		template_actions->setSpacing(6);
		for (size_t i = 0; i < lenses_hue_band_template_count(); ++i) {
			const struct lenses_hue_band_template *tmpl = lenses_hue_band_template_at(i);
			if (!tmpl)
				continue;
			auto *button = new QPushButton(obs_module_text(tmpl->label_key));
			button->setToolTip(obs_module_text(tmpl->description_key));
			const QColor chip = hue_color(tmpl->band.center_degrees, 255);
			button->setStyleSheet(QString("QPushButton { background-color: %1; color: #111;"
						      " border: 1px solid rgba(0,0,0,90);"
						      " border-radius: 6px; padding: 4px 10px; }")
						      .arg(chip.name(QColor::HexRgb)));
			QObject::connect(button, &QPushButton::clicked, this, [this, i]() {
				ApplyBandTemplate(i);
			});
			template_actions->addWidget(button);
		}
		template_actions->addStretch(1);
		control_layout->addLayout(template_actions, 3, 1, 1, 2);

		auto *actions = new QHBoxLayout();
		actions->setSpacing(8);
		auto *add_range = new QPushButton(obs_module_text("LensesFilter.InvertHueEditorAddRange"));
		auto *clear_ranges = new QPushButton(obs_module_text("LensesFilter.InvertHueEditorClearRanges"));
		actions->addWidget(add_range);
		actions->addWidget(clear_ranges);
		actions->addStretch(1);
		control_layout->addLayout(actions, 4, 0, 1, 3);

		root->addWidget(control_group);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
						      QDialogButtonBox::Apply);
		root->addWidget(buttons);

		QObject::connect(enabled_checkbox_, &QCheckBox::toggled, this,
				 [this](bool checked) {
					qualifier_enabled_ = checked;
					SyncPresetSelection();
				 });
		QObject::connect(mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			mode_ = mode_combo_->itemData(index).toInt();
			SyncPresetSelection();
		});
		QObject::connect(preset_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
			if (updating_preset_controls_)
				return;
			UpdatePresetDescription();
			const enum lenses_hue_preset_id selected =
				static_cast<enum lenses_hue_preset_id>(preset_combo_->currentData().toInt());
			preset_apply_button_->setEnabled(selected != LENSES_HUE_PRESET_CUSTOM);
		});
		QObject::connect(preset_apply_button_, &QPushButton::clicked, this, [this]() {
			const enum lenses_hue_preset_id selected =
				static_cast<enum lenses_hue_preset_id>(preset_combo_->currentData().toInt());
			ApplyPreset(selected);
		});

		spectrum_->SetBandSelectedCallback([this](int band_index) {
			selected_band_ = std::clamp(band_index, 0, (int)LENSES_INVERT_HUE_RANGE_MAX_BANDS - 1);
			if (QAbstractButton *button = band_group_->button(selected_band_))
				button->setChecked(true);
			SyncBandControls();
			SyncSpectrum();
		});
		spectrum_->SetCenterChangedCallback([this](float center_degrees) {
			auto &band = config_.bands[(size_t)selected_band_];
			if (!band.enabled)
				return;
			band.center_degrees = normalize_degrees(center_degrees);
			RefreshBandRows();
			SyncBandControls();
			SyncSpectrum();
			SyncPresetSelection();
		});

		QObject::connect(add_range, &QPushButton::clicked, this, [this]() {
			for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
				if (config_.bands[i].enabled)
					continue;
				config_.bands[i].enabled = true;
				selected_band_ = (int)i;
				if (QAbstractButton *button = band_group_->button(selected_band_))
					button->setChecked(true);
				RefreshBandRows();
				SyncBandControls();
				SyncSpectrum();
				SyncPresetSelection();
				return;
			}
		});

		QObject::connect(clear_ranges, &QPushButton::clicked, this, [this]() {
			for (auto &band : config_.bands)
				band.enabled = false;
			RefreshBandRows();
			SyncBandControls();
			SyncSpectrum();
			SyncPresetSelection();
		});

		QObject::connect(buttons, &QDialogButtonBox::clicked, this,
				 [this, buttons](QAbstractButton *button) {
					const auto role = buttons->buttonRole(button);
					if (role == QDialogButtonBox::ApplyRole) {
						if (on_apply_)
							on_apply_(qualifier_enabled_, mode_, config_);
						return;
					}
					if (role == QDialogButtonBox::AcceptRole) {
						if (on_apply_)
							on_apply_(qualifier_enabled_, mode_, config_);
						accept();
						return;
					}
					reject();
				 });

		RefreshBandRows();
		SyncBandControls();
		SyncSpectrum();
		SyncPresetSelection();
	}

	void SetApplyCallback(
		std::function<void(bool enabled, int mode, const struct lenses_invert_hue_range_config &config)>
			callback)
	{
		on_apply_ = std::move(callback);
	}

private:
	void AddBandControlRow(QGridLayout *layout, int row, const char *label_text, int min_value,
		       int max_value, QSlider *&slider_out, QDoubleSpinBox *&spin_out)
	{
		auto *label = new QLabel(label_text);
		auto *slider = new QSlider(Qt::Horizontal);
		auto *spin = new QDoubleSpinBox();
		slider->setRange(min_value, max_value);
		spin->setRange(min_value, max_value);
		spin->setDecimals(0);
		spin->setSingleStep(1.0);
		spin->setSuffix(" deg");
		layout->addWidget(label, row, 0);
		layout->addWidget(slider, row, 1);
		layout->addWidget(spin, row, 2);

		QObject::connect(slider, &QSlider::valueChanged, this, [spin](int value) {
			if (std::abs(spin->value() - (double)value) > 0.1)
				spin->setValue((double)value);
		});
		QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
				 [slider](double value) {
					const int rounded = std::clamp((int)std::lround(value), slider->minimum(),
							       slider->maximum());
					if (slider->value() != rounded)
						slider->setValue(rounded);
				 });

		QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
				 [this](double) { CommitBandControls(); });

		slider_out = slider;
		spin_out = spin;
	}

	void ApplyPreset(enum lenses_hue_preset_id preset_id)
	{
		if (preset_id == LENSES_HUE_PRESET_CUSTOM)
			return;

		bool preset_enabled = qualifier_enabled_;
		uint32_t preset_mode = (uint32_t)mode_;
		struct lenses_invert_hue_range_config preset_config = {};
		if (!lenses_hue_preset_apply(preset_id, &preset_enabled, &preset_mode, &preset_config))
			return;

		qualifier_enabled_ = preset_enabled;
		mode_ = (int)preset_mode;
		config_ = preset_config;
		selected_band_ = first_enabled_band(config_);

		updating_preset_controls_ = true;
		enabled_checkbox_->setChecked(qualifier_enabled_);
		const int mode_idx = mode_combo_->findData(mode_);
		if (mode_idx >= 0)
			mode_combo_->setCurrentIndex(mode_idx);
		updating_preset_controls_ = false;

		RefreshBandRows();
		SyncBandControls();
		SyncSpectrum();
		SyncPresetSelection();
	}

	void ApplyBandTemplate(size_t template_index)
	{
		const struct lenses_hue_band_template *tmpl = lenses_hue_band_template_at(template_index);
		if (!tmpl)
			return;
		if (selected_band_ < 0 || selected_band_ >= (int)LENSES_INVERT_HUE_RANGE_MAX_BANDS)
			return;

		config_.bands[(size_t)selected_band_] = tmpl->band;
		config_.bands[(size_t)selected_band_].enabled = 1;
		RefreshBandRows();
		SyncBandControls();
		SyncSpectrum();
		SyncPresetSelection();
	}

	void UpdatePresetDescription()
	{
		if (!preset_combo_ || !preset_description_label_)
			return;

		const enum lenses_hue_preset_id selected =
			static_cast<enum lenses_hue_preset_id>(preset_combo_->currentData().toInt());
		if (selected == LENSES_HUE_PRESET_CUSTOM) {
			preset_description_label_->setText(
				obs_module_text("LensesFilter.InvertHuePreset.Custom.Description"));
			return;
		}

		const struct lenses_hue_preset_definition *preset = preset_definition(selected);
		if (!preset || !preset->description_key) {
			preset_description_label_->setText("");
			return;
		}
		preset_description_label_->setText(obs_module_text(preset->description_key));
	}

	void SyncPresetSelection()
	{
		const enum lenses_hue_preset_id active = lenses_hue_preset_detect(
			qualifier_enabled_, (uint32_t)mode_, &config_);
		if (!preset_combo_)
			return;

		updating_preset_controls_ = true;
		int combo_idx = preset_combo_->findData((int)active);
		if (combo_idx < 0)
			combo_idx = preset_combo_->findData((int)LENSES_HUE_PRESET_CUSTOM);
		if (combo_idx >= 0 && preset_combo_->currentIndex() != combo_idx)
			preset_combo_->setCurrentIndex(combo_idx);
		updating_preset_controls_ = false;

		UpdatePresetDescription();
		if (preset_apply_button_) {
			const enum lenses_hue_preset_id selected =
				static_cast<enum lenses_hue_preset_id>(preset_combo_->currentData().toInt());
			preset_apply_button_->setEnabled(selected != LENSES_HUE_PRESET_CUSTOM);
		}
	}

	void CommitBandControls()
	{
		if (updating_controls_)
			return;

		auto &band = config_.bands[(size_t)selected_band_];
		band.center_degrees = clampf((float)center_spin_->value(), LENSES_INVERT_HUE_RANGE_CENTER_MIN,
					   LENSES_INVERT_HUE_RANGE_CENTER_MAX);
		band.width_degrees = clampf((float)width_spin_->value(), LENSES_INVERT_HUE_RANGE_WIDTH_MIN,
					  LENSES_INVERT_HUE_RANGE_WIDTH_MAX);
		band.softness_degrees =
			clampf((float)softness_spin_->value(), LENSES_INVERT_HUE_RANGE_SOFTNESS_MIN,
			       LENSES_INVERT_HUE_RANGE_SOFTNESS_MAX);
		band.center_degrees = normalize_degrees(band.center_degrees);

		RefreshBandRows();
		SyncSpectrum();
		SyncPresetSelection();
	}

	void SyncBandControls()
	{
		updating_controls_ = true;
		const auto &band = config_.bands[(size_t)selected_band_];

		center_spin_->setEnabled(band.enabled);
		width_spin_->setEnabled(band.enabled);
		softness_spin_->setEnabled(band.enabled);
		center_slider_->setEnabled(band.enabled);
		width_slider_->setEnabled(band.enabled);
		softness_slider_->setEnabled(band.enabled);

		center_spin_->setValue(band.center_degrees);
		width_spin_->setValue(band.width_degrees);
		softness_spin_->setValue(band.softness_degrees);

		updating_controls_ = false;
	}

	void SyncSpectrum()
	{
		spectrum_->SetConfig(config_);
		spectrum_->SetSelectedBand(selected_band_);
	}

	void RefreshBandRows()
	{
		for (uint32_t i = 0; i < LENSES_INVERT_HUE_RANGE_MAX_BANDS; ++i) {
			const auto &band = config_.bands[i];
			if (band_enabled_checks_[i] && band_enabled_checks_[i]->isChecked() != band.enabled)
				band_enabled_checks_[i]->setChecked(band.enabled);

			if (band_summary_labels_[i]) {
				if (!band.enabled) {
					band_summary_labels_[i]->setText(obs_module_text("LensesFilter.InvertHueEditorInactive"));
				} else {
					const QColor c = hue_color(band.center_degrees, 255);
					const QString swatch =
						QString("<span style='display:inline-block;width:10px;height:10px;"
							"border-radius:5px;background:%1;'></span>")
							.arg(c.name(QColor::HexRgb));
					band_summary_labels_[i]->setText(
						QString("%1 H:%2  W:%3  S:%4")
							.arg(swatch)
							.arg((int)std::lround(band.center_degrees))
							.arg((int)std::lround(band.width_degrees))
							.arg((int)std::lround(band.softness_degrees)));
				}
			}
		}
	}

	QCheckBox *enabled_checkbox_ = nullptr;
	QComboBox *mode_combo_ = nullptr;
	QComboBox *preset_combo_ = nullptr;
	QPushButton *preset_apply_button_ = nullptr;
	QLabel *preset_description_label_ = nullptr;
	HueSpectrumWidget *spectrum_ = nullptr;
	QButtonGroup *band_group_ = nullptr;
	std::array<QCheckBox *, LENSES_INVERT_HUE_RANGE_MAX_BANDS> band_enabled_checks_ = {};
	std::array<QLabel *, LENSES_INVERT_HUE_RANGE_MAX_BANDS> band_summary_labels_ = {};
	QSlider *center_slider_ = nullptr;
	QSlider *width_slider_ = nullptr;
	QSlider *softness_slider_ = nullptr;
	QDoubleSpinBox *center_spin_ = nullptr;
	QDoubleSpinBox *width_spin_ = nullptr;
	QDoubleSpinBox *softness_spin_ = nullptr;

	bool qualifier_enabled_ = true;
	int mode_ = LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE;
	struct lenses_invert_hue_range_config config_ = {};
	int selected_band_ = 0;
	bool updating_controls_ = false;
	bool updating_preset_controls_ = false;
	std::function<void(bool enabled, int mode, const struct lenses_invert_hue_range_config &config)> on_apply_;
};

void apply_hue_settings(struct lenses_filter_data *filter, bool qualifier_enabled, int mode,
			const struct lenses_invert_hue_range_config &config)
{
	if (!filter || !filter->context)
		return;

	struct lenses_invert_hue_range_config clamped = config;
	lenses_hue_qualifier_clamp(&clamped);

	obs_data_t *settings = obs_source_get_settings(filter->context);
	obs_data_set_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED, qualifier_enabled);
	obs_data_set_int(settings, SETTING_INVERT_HUE_QUALIFIER_MODE, mode);
	lenses_hue_qualifier_store_settings(settings, &clamped);
	obs_source_update(filter->context, settings);
	obs_data_release(settings);
}

} // namespace

extern "C" bool lenses_hue_qualifier_open_editor(struct lenses_filter_data *filter)
{
	if (!filter || !filter->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->context);
	if (!settings)
		return false;

	const bool qualifier_enabled = obs_data_get_bool(settings, SETTING_INVERT_HUE_QUALIFIER_ENABLED);
	const int mode = (int)obs_data_get_int(settings, SETTING_INVERT_HUE_QUALIFIER_MODE);
	struct lenses_invert_hue_range_config config = {};
	lenses_hue_qualifier_load_settings(settings, &config);
	obs_data_release(settings);

	QWidget *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	HueQualifierEditorDialog dialog(qualifier_enabled, mode, config, parent);
	dialog.SetApplyCallback([filter](bool enabled, int mode_value,
					 const struct lenses_invert_hue_range_config &updated_config) {
		apply_hue_settings(filter, enabled, mode_value, updated_config);
	});

	const int dialog_result = dialog.exec();
	return dialog_result == QDialog::Accepted;
}
