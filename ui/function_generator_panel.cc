#include "function_generator_panel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "common.h"
#include "core/device.h"

namespace m1k_scope::ui {

namespace {

// Frequency range of periodic waveforms, in Hertz. Above the maximum, a period
// has fewer than 10 samples at the M1K's sample rate.
constexpr double kMinFrequency = 0.1;
constexpr double kMaxFrequency = 10000.0;

// Levels of periodic waveforms on startup and when the source mode changes,
// in Volts and in Amperes.
constexpr double kDefaultMinVoltage = 1.0;
constexpr double kDefaultMaxVoltage = 4.0;
constexpr double kDefaultMinCurrent = 0.010;
constexpr double kDefaultMaxCurrent = 0.040;

// Returns the settings on startup, sourcing quantity.
core::GeneratorConfig GetDefaultConfig(core::Quantity quantity) {
  const bool current = quantity == core::Quantity::kCurrent;
  core::GeneratorConfig config;
  config.mode = core::GetSourceMode(quantity);
  config.min = current ? kDefaultMinCurrent : kDefaultMinVoltage;
  config.max = current ? kDefaultMaxCurrent : kDefaultMaxVoltage;
  return config;
}

}  // namespace

FunctionGeneratorChannelPanel::FunctionGeneratorChannelPanel(int ch,
                                                             QWidget* parent)
    : QWidget(parent), channel_(ch) {
  SetUpUi();
}

void FunctionGeneratorChannelPanel::SetUpUi() {
  auto* main_layout = new QVBoxLayout(this);
  SetPanelMargins(main_layout);
  main_layout->setSpacing(kWidgetSpacing);

  // 1. Enable checkbox: generate on this channel when checked, otherwise the
  // channel is left for measurement
  check_enable_ = new QCheckBox("Enable", this);
  main_layout->addWidget(check_enable_);

  // 2. Waveform Settings Group (Grayed out when not enabled)
  gen_group_ = new QGroupBox("Waveform Settings", this);
  form_ = CreateGroupForm(gen_group_);

  radio_voltage_ = new QRadioButton("Voltage", gen_group_);
  radio_current_ = new QRadioButton("Current", gen_group_);
  radio_voltage_->setChecked(true);
  auto* source_group = new QButtonGroup(this);
  source_group->addButton(radio_voltage_);
  source_group->addButton(radio_current_);
  auto* source_layout = new QHBoxLayout();
  source_layout->addWidget(radio_voltage_);
  source_layout->addWidget(radio_current_);
  form_->addRow("Source:", source_layout);

  combo_waveform_type_ = new QComboBox(gen_group_);
  for (const auto& [type, name] :
       {std::pair{core::WaveformType::kConstant, "Constant (DC)"},
        std::pair{core::WaveformType::kSine, "Sine"},
        std::pair{core::WaveformType::kSquare, "Square"},
        std::pair{core::WaveformType::kTriangle, "Triangle"},
        std::pair{core::WaveformType::kSawtooth, "Sawtooth"},
        std::pair{core::WaveformType::kStairstep, "Stairstep"}}) {
    combo_waveform_type_->addItem(name, static_cast<int>(type));
  }
  form_->addRow("Waveform:", combo_waveform_type_);

  auto add_spin = [this](const char* label) {
    auto* spin = new SpinBox(gen_group_);
    form_->addRow(label, spin);
    return spin;
  };
  spin_constant_ = add_spin("Level:");
  spin_frequency_ = add_spin("Frequency:");
  spin_min_ = add_spin("Min:");
  spin_max_ = add_spin("Max:");
  spin_phase_ = add_spin("Phase:");
  spin_duty_cycle_ = add_spin("Duty Cycle:");

  spin_frequency_->setRange(kMinFrequency, kMaxFrequency);
  spin_frequency_->setSuffix(" Hz");
  spin_min_->setToolTip("Lowest level of the waveform");
  spin_max_->setToolTip("Highest level of the waveform");
  spin_phase_->setRange(0.0, 360.0);
  spin_phase_->setSuffix(" °");
  spin_duty_cycle_->setRange(0.0, 100.0);
  spin_duty_cycle_->setSuffix(" %");
  spin_duty_cycle_->setToolTip("Share of the period the square wave is high");
  UpdateUnits();

  main_layout->addWidget(gen_group_);
  main_layout->addStretch();

  // Refresh the controls and notify of pending changes on every edit.
  auto on_modified = [this] {
    UpdateControls();
    emit PendingChangeOccurred();
  };

  connect(check_enable_, &QCheckBox::toggled, this, on_modified);
  // Only the source mode changes the units; updating them on other edits would
  // reformat the field being typed into.
  connect(radio_current_, &QRadioButton::toggled, this,
          &FunctionGeneratorChannelPanel::UpdateUnits);
  connect(radio_current_, &QRadioButton::toggled, this, on_modified);
  connect(combo_waveform_type_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          on_modified);
  for (auto* spin : {spin_constant_, spin_frequency_, spin_min_, spin_max_,
                     spin_phase_, spin_duty_cycle_}) {
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            on_modified);
  }

  LoadConfigToUi(GetDefaultConfig(core::Quantity::kVoltage));
  applied_config_ = CurrentConfig();
}

void FunctionGeneratorChannelPanel::UpdateControls() {
  // Inactive sections are grayed out (disabled) rather than hidden.
  gen_group_->setEnabled(check_enable_->isChecked());

  const auto type = static_cast<core::WaveformType>(
      combo_waveform_type_->currentData().toInt());
  const bool is_constant = type == core::WaveformType::kConstant;
  SetRowEnabled(form_, spin_constant_, is_constant);
  for (auto* spin : {spin_frequency_, spin_min_, spin_max_, spin_phase_}) {
    SetRowEnabled(form_, spin, !is_constant);
  }
  SetRowEnabled(form_, spin_duty_cycle_, type == core::WaveformType::kSquare);
}

void FunctionGeneratorChannelPanel::UpdateUnits() {
  // Levels may be anywhere in the output range of the source mode. Levels of
  // the other quantity mean nothing, so periodic waveforms start over at their
  // defaults.
  const core::Quantity quantity = GetSourcedQuantity();
  for (auto* spin : {spin_constant_, spin_min_, spin_max_}) {
    spin->SetQuantity(quantity);
  }
  const core::GeneratorConfig defaults = GetDefaultConfig(quantity);
  spin_min_->SetQuantityValue(defaults.min);
  spin_max_->SetQuantityValue(defaults.max);
}

// Returns the quantity the source mode chosen sources.
core::Quantity FunctionGeneratorChannelPanel::GetSourcedQuantity() const {
  return radio_current_->isChecked() ? core::Quantity::kCurrent
                                     : core::Quantity::kVoltage;
}

core::GeneratorConfig FunctionGeneratorChannelPanel::CurrentConfig() const {
  core::GeneratorConfig cfg;
  cfg.enabled = check_enable_->isChecked();
  cfg.mode = core::GetSourceMode(GetSourcedQuantity());
  cfg.type = static_cast<core::WaveformType>(
      combo_waveform_type_->currentData().toInt());
  cfg.constant = spin_constant_->GetQuantityValue();
  cfg.frequency = spin_frequency_->value();
  cfg.min = spin_min_->GetQuantityValue();
  cfg.max = spin_max_->GetQuantityValue();
  cfg.phase = spin_phase_->value();
  cfg.duty_cycle = spin_duty_cycle_->value() / 100;
  return cfg;
}

bool FunctionGeneratorChannelPanel::HasUnappliedChanges() const {
  return CurrentConfig() != applied_config_;
}

void FunctionGeneratorChannelPanel::Apply() {
  applied_config_ = CurrentConfig();
  emit ConfigApplied(channel_, applied_config_);
}

void FunctionGeneratorChannelPanel::Revert() {
  LoadConfigToUi(applied_config_);
  emit PendingChangeOccurred();
}

void FunctionGeneratorChannelPanel::LoadConfigToUi(
    const core::GeneratorConfig& config) {
  // Set the source mode first so the levels below use its units and range;
  // toggling the radio button updates them. Controls that do not change emit no
  // signal, so refresh the gray-out states explicitly.
  check_enable_->setChecked(config.enabled);
  const bool current = config.mode == core::SourceMode::kCurrent;
  (current ? radio_current_ : radio_voltage_)->setChecked(true);
  UpdateControls();

  const int type_idx =
      combo_waveform_type_->findData(static_cast<int>(config.type));
  if (type_idx >= 0) combo_waveform_type_->setCurrentIndex(type_idx);

  spin_constant_->SetQuantityValue(config.constant);
  spin_frequency_->setValue(config.frequency);
  spin_min_->SetQuantityValue(config.min);
  spin_max_->SetQuantityValue(config.max);
  spin_phase_->setValue(config.phase);
  spin_duty_cycle_->setValue(config.duty_cycle * 100);
}

FunctionGeneratorPanel::FunctionGeneratorPanel(core::Device& device,
                                               QWidget* parent)
    : QWidget(parent), device_(device) {
  SetUpUi();
  UpdateApplyRevertState();
}

void FunctionGeneratorPanel::SetUpUi() {
  auto* main_layout = new QVBoxLayout(this);
  SetPanelMargins(main_layout);
  main_layout->setSpacing(kSectionSpacing);

  // Apply/Revert bar
  auto* button_bar_layout = new QHBoxLayout();
  button_bar_layout->setSpacing(kWidgetSpacing);

  btn_apply_ = CreateActionButton("&Apply", this);
  button_bar_layout->addWidget(btn_apply_);
  btn_revert_ = CreateActionButton("&Revert", this);
  button_bar_layout->addWidget(btn_revert_);
  main_layout->addLayout(button_bar_layout);

  // Channel panels stacked vertically
  for (int i = 0; i < core::m1k::kChannelCount; ++i) {
    auto* group =
        new QGroupBox(QString("Channel %1").arg(GetChannelLetter(i)), this);
    auto* group_layout = new QVBoxLayout(group);
    group_layout->setContentsMargins(0, 0, 0, 0);
    channel_panels_[i] = new FunctionGeneratorChannelPanel(i, group);
    group_layout->addWidget(channel_panels_[i]);
    main_layout->addWidget(group);

    connect(channel_panels_[i], &FunctionGeneratorChannelPanel::ConfigApplied,
            this, [this](int ch, const core::GeneratorConfig& config) {
              if (!device_.SetGeneratorConfig(ch, config)) {
                ShowError(this,
                          QString("Failed to apply the function generator "
                                  "settings of channel %1.")
                              .arg(GetChannelLetter(ch)));
              }
            });
    connect(channel_panels_[i],
            &FunctionGeneratorChannelPanel::PendingChangeOccurred, this,
            &FunctionGeneratorPanel::UpdateApplyRevertState);
  }
  main_layout->addStretch();

  connect(btn_apply_, &QPushButton::clicked, this,
          &FunctionGeneratorPanel::ApplyChanges);
  connect(btn_revert_, &QPushButton::clicked, this,
          &FunctionGeneratorPanel::RevertChanges);
}

void FunctionGeneratorPanel::ApplyChanges() {
  for (auto* panel : channel_panels_) {
    panel->Apply();
  }
  UpdateApplyRevertState();
}

void FunctionGeneratorPanel::RevertChanges() {
  for (auto* panel : channel_panels_) {
    panel->Revert();
  }
  UpdateApplyRevertState();
}

void FunctionGeneratorPanel::UpdateApplyRevertState() {
  const bool has_changes = std::ranges::any_of(
      channel_panels_,
      [](const auto* panel) { return panel->HasUnappliedChanges(); });
  btn_apply_->setEnabled(has_changes);
  btn_revert_->setEnabled(has_changes);
}

}  // namespace m1k_scope::ui
