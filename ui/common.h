#pragma once

#include <QChar>
#include <QColor>
#include <QDoubleSpinBox>
#include <QPixmap>
#include <QString>

#include "core/measurement.h"

class QComboBox;
class QFormLayout;
class QLayout;
class QPushButton;
class QWidget;

// Building blocks shared by the panels and plots: layout metrics, how
// measurements are shown, and widget helpers.
namespace m1k_scope::ui {

// Layout metrics shared by the window and its panels, in pixels.
constexpr int kPanelMargin = 6;          // Around the content of a panel
constexpr int kWidgetSpacing = 6;        // Between widgets side by side
constexpr int kSectionSpacing = 8;       // Between the sections of a panel
constexpr int kRowSpacing = 4;           // Between the rows of a list or form
constexpr int kGroupMarginX = 8;         // Left and right inside a group box
constexpr int kGroupMarginY = 6;         // Top and bottom inside a group box
constexpr int kActionButtonHeight = 32;  // Pause/Resume, Sweep, Apply/Revert
constexpr int kSwatchSize = 16;          // Side of a measurement color swatch

// Width of the controls of a mode. 400px leaves ample room for the controls and
// the whole tab bar of the Realtime mode. The window minimum size is derived by
// QMainWindow from the layouts, so the controls are never squished.
constexpr int kModePanelWidth = 400;

// The UI shows current in mA; the core stores Amperes.
constexpr double kMaPerAmpere = 1000.0;

// Sets the margins of layout, the layout of a panel, to kPanelMargin.
void SetPanelMargins(QLayout* layout);

// Returns a new form laid out in group, a group box, with the margins and row
// spacing of a group.
QFormLayout* CreateGroupForm(QWidget* group);

// Enables or grays out field, a field of form, and its label. Inactive rows are
// grayed out rather than hidden, so the layout does not jump.
void SetRowEnabled(QFormLayout* form, QWidget* field, bool enabled);

// Returns a new button labeled text, as tall as the main action of a panel.
QPushButton* CreateActionButton(const QString& text, QWidget* parent);

// Shows message as an error in a dialog over the window of parent, titled like
// that window.
void ShowError(QWidget* parent, const QString& message);

// Tells the user that the M1K failed to start acquiring.
void ShowStartFailure(QWidget* parent);

// Letter naming channel ch, e.g. 'A'.
QChar GetChannelLetter(int ch);

// Name of measurement m, an index into core::kMeasurements, e.g. "Ch A - V".
QString GetMeasurementName(int m);

// Trace color of measurement m, an index into core::kMeasurements.
QColor GetMeasurementColor(int m);

// Square of the trace color of measurement m, kSwatchSize pixels wide.
QPixmap GetSwatch(int m);

// Whether the trace of measurement m is shown on startup: only the voltages.
bool IsShownByDefault(int m);

// Adds an item per measurement to combo, with its swatch and name, and its
// index into core::kMeasurements as item data.
void AddMeasurementItems(QComboBox* combo);

// Spin box that is easy to type into: a click selects the whole value, and
// numbers out of range or with too many decimals can be typed freely, then get
// clamped and rounded on Enter or focus out.
class SpinBox : public QDoubleSpinBox {
 public:
  using QDoubleSpinBox::QDoubleSpinBox;

  // Shows a value of quantity: in its unit (V or mA), over the range the M1K
  // sources and measures, with a matching number of decimals and step.
  void SetQuantity(core::Quantity quantity);

  // Returns the value in the unit of the quantity set with SetQuantity(), such
  // as Amperes for a value shown in mA, else the value shown.
  double GetQuantityValue() const;

  // Shows value, in the unit of the quantity set with SetQuantity().
  void SetQuantityValue(double value);

 protected:
  QValidator::State validate(QString& text, int& pos) const override;
  void fixup(QString& input) const override;
  void focusInEvent(QFocusEvent* event) override;

 private:
  // Parses the number in text, ignoring the range and decimals limits.
  double Parse(const QString& text, bool* ok = nullptr) const;

  // Unit shown per unit of the quantity, such as mA per Ampere.
  double unit_scale_{1.0};
};

}  // namespace m1k_scope::ui
