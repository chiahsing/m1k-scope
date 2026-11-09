#pragma once

#include <QWidget>
#include <array>

#include "core/generator.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QPushButton;
class QRadioButton;

namespace m1k_scope::core {
class Device;
}  // namespace m1k_scope::core

namespace m1k_scope::ui {

class SpinBox;

// Function generator settings for a single channel.
//
// Hosted by FunctionGeneratorPanel, one per channel. Encapsulates the enable
// checkbox, whether the channel sources voltage or current, and the waveform
// parameters. Levels are shown in V or mA depending on the source mode.
//
// Inactive controls remain visible and are grayed out (disabled) instead of
// hidden. Changes only take effect upon invoking Apply(), and can be restored
// to the last applied state via Revert().
class FunctionGeneratorChannelPanel : public QWidget {
  Q_OBJECT

 public:
  // Constructs the panel of channel ch.
  explicit FunctionGeneratorChannelPanel(int ch, QWidget* parent = nullptr);
  ~FunctionGeneratorChannelPanel() override = default;

  // Reads the current UI control values (pending/editing state).
  core::GeneratorConfig CurrentConfig() const;

  // Returns true if any control value differs from the last applied config.
  bool HasUnappliedChanges() const;

  // Commits the current UI state as applied configuration and emits
  // ConfigApplied.
  void Apply();

  // Restores all UI controls back to the last applied configuration.
  void Revert();

 signals:
  // Emitted by Apply() with the channel and its new settings.
  void ConfigApplied(int ch, const core::GeneratorConfig& config);

  // Emitted whenever the user interacts with any control in this panel.
  void PendingChangeOccurred();

 private:
  void SetUpUi();
  void UpdateControls();
  void UpdateUnits();
  core::Quantity GetSourcedQuantity() const;
  void LoadConfigToUi(const core::GeneratorConfig& config);

  int channel_;
  core::GeneratorConfig applied_config_;

  // Enables the function generator on this channel
  QCheckBox* check_enable_{nullptr};

  // Waveform settings group widgets (grayed out when not enabled). Fields that
  // do not apply to the selected waveform are grayed out as well.
  QGroupBox* gen_group_{nullptr};
  QFormLayout* form_{nullptr};
  QRadioButton* radio_voltage_{nullptr};
  QRadioButton* radio_current_{nullptr};
  QComboBox* combo_waveform_type_{nullptr};
  SpinBox* spin_constant_{nullptr};
  SpinBox* spin_frequency_{nullptr};
  SpinBox* spin_min_{nullptr};
  SpinBox* spin_max_{nullptr};
  SpinBox* spin_phase_{nullptr};
  SpinBox* spin_duty_cycle_{nullptr};
};

// Content of the "Function Generator" tab.
//
// Stacks one FunctionGeneratorChannelPanel per channel under Apply and Revert
// buttons. Edits stay pending until Apply is clicked, which sends them to the
// Device, and Revert restores the last applied settings.
class FunctionGeneratorPanel : public QWidget {
  Q_OBJECT

 public:
  // Constructs the panel. device must outlive this panel.
  explicit FunctionGeneratorPanel(core::Device& device,
                                  QWidget* parent = nullptr);
  ~FunctionGeneratorPanel() override = default;

 private slots:
  void ApplyChanges();
  void RevertChanges();
  void UpdateApplyRevertState();

 private:
  void SetUpUi();

  core::Device& device_;

  QPushButton* btn_apply_{nullptr};
  QPushButton* btn_revert_{nullptr};

  // Channel panels, indexed by channel
  std::array<FunctionGeneratorChannelPanel*, core::m1k::kChannelCount>
      channel_panels_{};
};

}  // namespace m1k_scope::ui
