#pragma once

#include "core/bode_sweep.h"
#include "main_window.h"

class QPushButton;
class QSpinBox;

namespace m1k_scope::ui {

class BodePlotWidget;
class SpinBox;

// Controls of the "Bode" mode of the main window, running a core::BodeSweep
// for the BodePlotWidget.
//
// The Sweep button on top sweeps the frequencies and the sine wave set below
// it, and turns into a Stop button while sweeping. The plot adds each point as
// it is measured.
class BodePanel : public ModePanel {
  Q_OBJECT

 public:
  // Constructs the panel controlling bode_plot_widget. device and
  // bode_plot_widget must outlive this panel.
  BodePanel(core::Device& device, BodePlotWidget& bode_plot_widget,
            QWidget* parent = nullptr);
  ~BodePanel() override = default;

  void Activate() override;
  void Deactivate() override;

 private:
  void SetUpUi();
  void ConnectSignals();
  void UpdateSweepButton();
  void ToggleSweep();
  void StartSweep();
  void StopSweep();
  void TakeFrame();

  BodePlotWidget& bode_plot_widget_;
  QPushButton* btn_sweep_{nullptr};  // "Sweep", or "Stop" while sweeping
  SpinBox* spin_from_{nullptr};      // In Hertz
  SpinBox* spin_to_{nullptr};        // In Hertz
  QSpinBox* spin_points_{nullptr};   // Points per decade
  SpinBox* spin_min_{nullptr};       // Lowest level of the sine wave
  SpinBox* spin_max_{nullptr};       // Highest level of the sine wave

  core::BodeSweep sweep_;
};

}  // namespace m1k_scope::ui
