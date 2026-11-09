#include "common.h"

#include <QComboBox>
#include <QFormLayout>
#include <QIcon>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <iterator>

namespace m1k_scope::ui {

namespace {

// Trace colors, indexed like core::kMeasurements.
const QColor kMeasurementColors[] = {
    QColor(255, 215, 0),   // Ch A voltage: yellow
    QColor(80, 220, 80),   // Ch A current: green
    QColor(0, 200, 255),   // Ch B voltage: cyan
    QColor(255, 80, 200),  // Ch B current: magenta
};
static_assert(std::ssize(kMeasurementColors) == core::kMeasurementCount);

}  // namespace

void SetPanelMargins(QLayout* layout) {
  layout->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin,
                             kPanelMargin);
}

QFormLayout* CreateGroupForm(QWidget* group) {
  auto* form = new QFormLayout(group);
  form->setContentsMargins(kGroupMarginX, kGroupMarginY, kGroupMarginX,
                           kGroupMarginY);
  form->setVerticalSpacing(kRowSpacing);
  return form;
}

void SetRowEnabled(QFormLayout* form, QWidget* field, bool enabled) {
  field->setEnabled(enabled);
  form->labelForField(field)->setEnabled(enabled);
}

QPushButton* CreateActionButton(const QString& text, QWidget* parent) {
  auto* button = new QPushButton(text, parent);
  button->setFixedHeight(kActionButtonHeight);
  return button;
}

void ShowError(QWidget* parent, const QString& message) {
  QMessageBox::critical(parent, parent->window()->windowTitle(), message);
}

void ShowStartFailure(QWidget* parent) {
  ShowError(parent, "Failed to start acquisition.");
}

QChar GetChannelLetter(int ch) { return QChar('A' + ch); }

QString GetMeasurementName(int m) {
  const core::Measurement& measurement = core::kMeasurements[m];
  return QString("Ch %1 - %2")
      .arg(GetChannelLetter(measurement.channel))
      .arg(measurement.quantity == core::Quantity::kCurrent ? "I" : "V");
}

QColor GetMeasurementColor(int m) { return kMeasurementColors[m]; }

QPixmap GetSwatch(int m) {
  QPixmap pixmap(kSwatchSize, kSwatchSize);
  pixmap.fill(GetMeasurementColor(m));
  return pixmap;
}

bool IsShownByDefault(int m) {
  return core::kMeasurements[m].quantity == core::Quantity::kVoltage;
}

void AddMeasurementItems(QComboBox* combo) {
  for (int m = 0; m < core::kMeasurementCount; ++m) {
    combo->addItem(QIcon(GetSwatch(m)), GetMeasurementName(m), m);
  }
}

void SpinBox::SetQuantity(core::Quantity quantity) {
  const bool current = quantity == core::Quantity::kCurrent;
  unit_scale_ = current ? kMaPerAmpere : 1.0;
  setRange(core::GetMinValue(quantity) * unit_scale_,
           core::GetMaxValue(quantity) * unit_scale_);
  setDecimals(current ? 1 : 3);
  setSingleStep(current ? 1.0 : 0.1);
  setSuffix(current ? " mA" : " V");
}

double SpinBox::GetQuantityValue() const { return value() / unit_scale_; }

void SpinBox::SetQuantityValue(double value) { setValue(value * unit_scale_); }

QValidator::State SpinBox::validate(QString& text, int& pos) const {
  const QValidator::State state = QDoubleSpinBox::validate(text, pos);
  bool is_number = false;
  Parse(text, &is_number);
  return state == QValidator::Invalid && is_number ? QValidator::Intermediate
                                                   : state;
}

void SpinBox::fixup(QString& input) const {
  input =
      textFromValue(std::clamp(Parse(input), minimum(), maximum())) + suffix();
}

void SpinBox::focusInEvent(QFocusEvent* event) {
  QDoubleSpinBox::focusInEvent(event);
  // Defer until the click that gave focus has placed the cursor.
  QTimer::singleShot(0, this, &QAbstractSpinBox::selectAll);
}

double SpinBox::Parse(const QString& text, bool* ok) const {
  return locale().toDouble(QString(text).remove(suffix()).trimmed(), ok);
}

}  // namespace m1k_scope::ui
