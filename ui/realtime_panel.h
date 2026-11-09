#pragma once

#include <array>
#include <chrono>
#include <optional>

#include "core/frame_trigger.h"
#include "core/generator.h"
#include "main_window.h"

class QPushButton;

namespace m1k_scope::ui {

class OscilloscopePanel;
class WaveformWidget;

// Controls of the "Realtime" mode of the main window, the M1K acquiring
// continuously, for the WaveformWidget plotting it.
//
// A Pause/Resume button on top, then a QTabWidget with two tabs:
//   * Oscilloscope: OscilloscopePanel with the X and Y axes and the trigger.
//   * Function Generator: FunctionGeneratorPanel with per-channel generator
//     settings and Apply/Revert buttons.
//
// Acquisition runs while the mode is active, unless paused with Pause/Resume.
// Deactivating the mode, by switching to another one, pauses it, and
// activating it again restores the state it had. The function generators,
// trigger and time window it set in the Device, which another mode may change,
// are restored too.
class RealtimePanel : public ModePanel {
  Q_OBJECT

 public:
  // Constructs the panel controlling waveform_widget. device and
  // waveform_widget must outlive this panel.
  RealtimePanel(core::Device& device, WaveformWidget& waveform_widget,
                QWidget* parent = nullptr);
  ~RealtimePanel() override = default;

  // Pauses acquisition if it runs, else resumes it.
  void TogglePause();

  void Activate() override;
  void Deactivate() override;

 private:
  // The Device's setup of this mode, which another mode may change.
  struct DeviceSetup {
    std::array<core::GeneratorConfig, core::m1k::kChannelCount> generators;
    core::TriggerConfig trigger;
    std::chrono::microseconds time_window;
  };

  void SetUpUi();
  void ConnectSignals();
  void StartAcquisition();
  void UpdatePauseButton();

  WaveformWidget& waveform_widget_;
  QPushButton* btn_pause_{nullptr};  // "Pause" while acquiring, else "Resume"
  OscilloscopePanel* oscilloscope_panel_{nullptr};

  // Whether acquisition runs while the mode is active: from the start, and
  // after that as it was when the mode was last deactivated.
  bool runs_when_active_{true};

  // Saved on deactivation and restored on activation, once deactivated.
  std::optional<DeviceSetup> saved_setup_;
};

}  // namespace m1k_scope::ui
