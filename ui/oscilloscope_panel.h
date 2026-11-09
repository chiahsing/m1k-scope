#pragma once

#include <QGroupBox>
#include <QWidget>
#include <array>
#include <optional>

#include "core/measurement.h"
#include "plot_widget.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QStackedWidget;

namespace m1k_scope::core {
class Device;
}  // namespace m1k_scope::core

namespace m1k_scope::ui {

class SpinBox;

// Controls of the view of one quantity on an axis of one orientation, side by
// side: its value per division, from the 1-2-5 sequence of its Axis, and the
// value at its center. It only reports edits; ShowView() keeps it in step with
// views changed elsewhere, such as with the mouse wheel.
class ViewControls : public QWidget {
  Q_OBJECT

 public:
  // Constructs the controls of quantity on the axis of orientation.
  ViewControls(Orientation orientation, core::Quantity quantity,
               QWidget* parent = nullptr);
  ~ViewControls() override = default;

  // Switches to quantity, at its default view, unless it already shows it,
  // without emitting the signals.
  void SetQuantity(core::Quantity quantity);

  // Shows a view of quantity on the axis of orientation, without emitting the
  // signals, if those are what it controls.
  void ShowView(Orientation orientation, core::Quantity quantity,
                int scale_index, double center);

 signals:
  // Emitted when the value per division is picked, as an index into
  // GetAxis(quantity).per_division.
  void ScaleChanged(Orientation orientation, core::Quantity quantity,
                    int scale_index);

  // Emitted when the value at the center is set, in the quantity's unit.
  void CenterChanged(Orientation orientation, core::Quantity quantity,
                     double center);

 private:
  void ShowQuantity();
  void ShowView(int scale_index, double center);

  const Orientation orientation_;
  core::Quantity quantity_;
  QComboBox* scale_{nullptr};  // Item index is the scale index
  SpinBox* center_{nullptr};   // In the axis' shown unit
};

// "X axis" group of the Oscilloscope tab.
//
// Picks what the bottom axis shows: time, or a measurement to plot the others
// against (X-Y). The Scale row sets the time per division, or the view of the
// measurement, separate from the vertical axes. In X-Y, the Window row sets
// how long a frame is, as the time per division does. It only reports edits;
// ShowTimeScale() and ShowXView() keep it in step with zooms changed
// elsewhere.
class XAxisPanel : public QGroupBox {
  Q_OBJECT

 public:
  explicit XAxisPanel(QWidget* parent = nullptr);
  ~XAxisPanel() override = default;

  // Shows the time per division, an index into GetTimesPerDivision(), without
  // emitting the signals.
  void ShowTimeScale(int scale_index);

  // Shows the view of quantity on the axis of orientation, without emitting
  // the signals, if it is the X measurement's on the bottom axis.
  void ShowView(Orientation orientation, core::Quantity quantity,
                int scale_index, double center);

 signals:
  // Emitted when measurement m, an index into core::kMeasurements, is picked
  // for the bottom axis, or nullopt for time.
  void XMeasurementChanged(std::optional<int> m);

  // Emitted when the value per division of the X measurement is picked, as an
  // index into GetAxis(quantity).per_division.
  void ScaleChanged(Orientation orientation, core::Quantity quantity,
                    int scale_index);

  // Emitted when the value at the center of the bottom axis is set, in the X
  // measurement's unit.
  void CenterChanged(Orientation orientation, core::Quantity quantity,
                     double center);

  // Emitted when the time per division, or the time window, is picked, as an
  // index into GetTimesPerDivision().
  void TimeScaleChanged(int scale_index);

 private:
  void PickSource();

  QFormLayout* form_{nullptr};
  QComboBox* combo_source_{nullptr};  // Item data is the measurement, if any
  QStackedWidget* scale_{nullptr};    // combo_time_ or x_view_
  QComboBox* combo_time_{nullptr};    // Item index is the time scale index
  ViewControls* x_view_{nullptr};
  QComboBox* combo_window_{nullptr};  // Item index is the time scale index
};

// "Y axis" group of the Oscilloscope tab.
//
// A checkbox per measurement, with its color swatch and name, shows or hides
// its trace; the X measurement's is grayed out, as its trace is not drawn.
// Below, a row per vertical axis sets its view. It only reports edits;
// ShowView() keeps it in step with zooms changed elsewhere.
class YAxisPanel : public QGroupBox {
  Q_OBJECT

 public:
  explicit YAxisPanel(QWidget* parent = nullptr);
  ~YAxisPanel() override = default;

  // Grays out the checkbox of measurement m, an index into
  // core::kMeasurements, as the X measurement, or none for nullopt.
  void SetXMeasurement(std::optional<int> m);

  // Shows the view of quantity on the axis of orientation, without emitting
  // the signals, if it is on a vertical axis.
  void ShowView(Orientation orientation, core::Quantity quantity,
                int scale_index, double center);

 signals:
  // Emitted when the trace of measurement m, an index into
  // core::kMeasurements, is checked to be shown or hidden.
  void MeasurementVisibilityChanged(int m, bool visible);

  // Emitted when the value per division of the vertical axis of quantity is
  // picked, as an index into GetAxis(quantity).per_division.
  void ScaleChanged(Orientation orientation, core::Quantity quantity,
                    int scale_index);

  // Emitted when the value at the center of the vertical axis of quantity is
  // set, in the quantity's unit.
  void CenterChanged(Orientation orientation, core::Quantity quantity,
                     double center);

 private:
  std::array<QCheckBox*, core::kMeasurementCount> checks_{};

  // Indexed by core::Quantity.
  std::array<ViewControls*, std::size(kAxisQuantities)> views_{};
};

// "Trigger" group of the Oscilloscope tab.
//
// Picks at most one measurement to trigger on, with its edge, level and mode.
// The level is shown in V or mA depending on the source quantity. The other
// settings are grayed out while the trigger is off. Settings live in
// the Device and take effect right away.
class TriggerPanel : public QGroupBox {
  Q_OBJECT

 public:
  // Constructs the panel. device must outlive this panel.
  explicit TriggerPanel(core::Device& device, QWidget* parent = nullptr);
  ~TriggerPanel() override = default;

 signals:
  // Emitted whenever the trigger settings in the Device change.
  void TriggerChanged();

 private:
  void UpdateControls();
  void SetLevelQuantity(core::Quantity quantity);
  void Apply();

  core::Device& device_;

  QFormLayout* form_{nullptr};
  // Item data is the index into core::kMeasurements, or core::kNoTrigger.
  QComboBox* combo_source_{nullptr};
  QWidget* edge_widget_{nullptr};
  QButtonGroup* edge_group_{nullptr};  // Button ids are core::TriggerEdge
  SpinBox* spin_level_{nullptr};
  QWidget* mode_widget_{nullptr};
  QButtonGroup* mode_group_{nullptr};  // Button ids are core::TriggerMode

  // Quantity whose unit the level is shown in.
  core::Quantity level_quantity_{core::Quantity::kVoltage};
};

// Content of the "Oscilloscope" tab.
//
// The XAxisPanel and YAxisPanel, and the TriggerPanel at the bottom.
class OscilloscopePanel : public QWidget {
  Q_OBJECT

 public:
  // Constructs the panel. device must outlive this panel.
  explicit OscilloscopePanel(core::Device& device, QWidget* parent = nullptr);
  ~OscilloscopePanel() override = default;

  // The panels setting the axes of the waveform display.
  XAxisPanel* x_axis_panel() const { return x_axis_panel_; }
  YAxisPanel* y_axis_panel() const { return y_axis_panel_; }

 signals:
  // Emitted whenever the trigger settings in the Device change.
  void SettingsChanged();

 private:
  XAxisPanel* x_axis_panel_{nullptr};
  YAxisPanel* y_axis_panel_{nullptr};
};

}  // namespace m1k_scope::ui
