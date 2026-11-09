#include "bode_panel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "bode_plot_widget.h"
#include "common.h"
#include "core/device.h"

namespace m1k_scope::ui {

namespace {

// Frequencies a sweep may span, in Hertz, and those on startup. The highest
// keeps several samples per period.
constexpr double kMinFrequency = 1;
constexpr double kMaxFrequency = core::m1k::kSampleRate / 5;
constexpr double kDefaultFrom = 10;
constexpr double kDefaultTo = 10000;
constexpr int kFrequencyDecimals = 1;

// Points per decade a sweep may take, and on startup.
constexpr int kMinPointsPerDecade = 1;
constexpr int kMaxPointsPerDecade = 50;
constexpr int kDefaultPointsPerDecade = 10;

// The sine wave on startup, in Volts: the whole range Ch A sources.
constexpr double kDefaultMin = core::m1k::kMinVoltage;
constexpr double kDefaultMax = core::m1k::kMaxVoltage;

// Sets spin to show a frequency in Hertz.
void SetUpFrequencySpin(SpinBox* spin, double frequency) {
  spin->setRange(kMinFrequency, kMaxFrequency);
  spin->setDecimals(kFrequencyDecimals);
  spin->setSuffix(" Hz");
  spin->setValue(frequency);
}

}  // namespace

BodePanel::BodePanel(core::Device& device, BodePlotWidget& bode_plot_widget,
                     QWidget* parent)
    : ModePanel(device, parent),
      bode_plot_widget_(bode_plot_widget),
      sweep_(device) {
  SetUpUi();
  ConnectSignals();
}

void BodePanel::SetUpUi() {
  auto* layout = new QVBoxLayout(this);
  SetPanelMargins(layout);
  layout->setSpacing(kSectionSpacing);

  btn_sweep_ = CreateActionButton("Sweep", this);
  layout->addWidget(btn_sweep_);

  // The frequencies swept.
  auto* sweep = new QGroupBox("Sweep", this);
  auto* sweep_form = CreateGroupForm(sweep);
  spin_from_ = new SpinBox(sweep);
  spin_to_ = new SpinBox(sweep);
  SetUpFrequencySpin(spin_from_, kDefaultFrom);
  SetUpFrequencySpin(spin_to_, kDefaultTo);
  auto* frequencies = new QHBoxLayout();
  frequencies->setSpacing(kWidgetSpacing);
  frequencies->addWidget(spin_from_, 1);
  frequencies->addWidget(new QLabel("to", sweep));
  frequencies->addWidget(spin_to_, 1);
  sweep_form->addRow("From:", frequencies);
  spin_points_ = new QSpinBox(sweep);
  spin_points_->setRange(kMinPointsPerDecade, kMaxPointsPerDecade);
  spin_points_->setValue(kDefaultPointsPerDecade);
  sweep_form->addRow("Points per decade:", spin_points_);
  layout->addWidget(sweep);

  // The sine wave Ch A drives the input with.
  auto* input =
      new QGroupBox(QString("Input: Ch %1")
                        .arg(GetChannelLetter(core::BodeSweep::kInputChannel)),
                    this);
  auto* input_form = CreateGroupForm(input);
  spin_min_ = new SpinBox(input);
  spin_min_->SetQuantity(core::Quantity::kVoltage);
  spin_min_->SetQuantityValue(kDefaultMin);
  input_form->addRow("Min:", spin_min_);
  spin_max_ = new SpinBox(input);
  spin_max_->SetQuantity(core::Quantity::kVoltage);
  spin_max_->SetQuantityValue(kDefaultMax);
  input_form->addRow("Max:", spin_max_);
  layout->addWidget(input);

  layout->addWidget(
      new QLabel(QString("Output: Ch %1, Hi-Z")
                     .arg(GetChannelLetter(core::BodeSweep::kOutputChannel)),
                 this));

  layout->addStretch();
  setFixedWidth(kModePanelWidth);
}

void BodePanel::ConnectSignals() {
  connect(btn_sweep_, &QPushButton::clicked, this, &BodePanel::ToggleSweep);
  for (SpinBox* spin : {spin_from_, spin_to_, spin_min_, spin_max_}) {
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &BodePanel::UpdateSweepButton);
  }
}

// Enables the Sweep button while the frequencies and the levels rise, as a
// sine wave without amplitude has no response to measure, or while a sweep
// runs, to stop it.
void BodePanel::UpdateSweepButton() {
  const bool sweeping = sweep_.IsRunning();
  btn_sweep_->setText(sweeping ? "Stop" : "Sweep");
  btn_sweep_->setEnabled(sweeping || (spin_from_->value() < spin_to_->value() &&
                                      spin_min_->value() < spin_max_->value()));
}

void BodePanel::Activate() {
  // Notified on the acquisition thread; take the frame on the GUI thread.
  device_.SetFrameReadyCallback([this] {
    QMetaObject::invokeMethod(this, &BodePanel::TakeFrame,
                              Qt::QueuedConnection);
  });
}

void BodePanel::Deactivate() { StopSweep(); }

void BodePanel::ToggleSweep() {
  if (sweep_.IsRunning()) {
    StopSweep();
  } else {
    StartSweep();
  }
}

void BodePanel::StartSweep() {
  const core::BodeSweep::Settings settings = {
      .from = spin_from_->value(),
      .to = spin_to_->value(),
      .points_per_decade = spin_points_->value(),
      .min = spin_min_->GetQuantityValue(),
      .max = spin_max_->GetQuantityValue()};
  bode_plot_widget_.SetFrequencyRange(settings.from, settings.to);
  if (!sweep_.Start(settings)) ShowStartFailure(this);
  UpdateSweepButton();
}

void BodePanel::StopSweep() {
  sweep_.Stop();
  UpdateSweepButton();
}

// Plots the point a frame completes, and shows when the sweep ends with it.
void BodePanel::TakeFrame() {
  if (const auto point = sweep_.TakeFrame()) {
    bode_plot_widget_.AddPoint(*point);
    UpdateSweepButton();
  }
}

}  // namespace m1k_scope::ui
