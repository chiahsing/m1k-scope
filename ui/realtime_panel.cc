#include "realtime_panel.h"

#include <QMetaObject>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "common.h"
#include "core/device.h"
#include "function_generator_panel.h"
#include "oscilloscope_panel.h"
#include "waveform_widget.h"

namespace m1k_scope::ui {

RealtimePanel::RealtimePanel(core::Device& device,
                             WaveformWidget& waveform_widget, QWidget* parent)
    : ModePanel(device, parent), waveform_widget_(waveform_widget) {
  SetUpUi();
  ConnectSignals();
}

void RealtimePanel::SetUpUi() {
  auto* layout = new QVBoxLayout(this);
  SetPanelMargins(layout);
  layout->setSpacing(kSectionSpacing);

  btn_pause_ = CreateActionButton("", this);
  UpdatePauseButton();
  layout->addWidget(btn_pause_);

  auto* tabs = new QTabWidget(this);
  oscilloscope_panel_ = new OscilloscopePanel(device_, tabs);
  tabs->addTab(oscilloscope_panel_, "Oscilloscope");
  tabs->addTab(new FunctionGeneratorPanel(device_, tabs), "Function Generator");
  layout->addWidget(tabs);
  setFixedWidth(kModePanelWidth);
}

void RealtimePanel::ConnectSignals() {
  connect(btn_pause_, &QPushButton::clicked, this, &RealtimePanel::TogglePause);

  // Keep the axes of the display and their panels in step.
  XAxisPanel* x_axis = oscilloscope_panel_->x_axis_panel();
  connect(x_axis, &XAxisPanel::XMeasurementChanged, &waveform_widget_,
          &WaveformWidget::SetXMeasurement);
  connect(x_axis, &XAxisPanel::TimeScaleChanged, &waveform_widget_,
          &WaveformWidget::SetTimeScale);
  connect(&waveform_widget_, &WaveformWidget::TimeScaleChanged, x_axis,
          &XAxisPanel::ShowTimeScale);
  connect(x_axis, &XAxisPanel::ScaleChanged, &waveform_widget_,
          &WaveformWidget::SetScale);
  connect(x_axis, &XAxisPanel::CenterChanged, &waveform_widget_,
          &WaveformWidget::SetCenter);
  connect(&waveform_widget_, &WaveformWidget::ViewChanged, x_axis,
          &XAxisPanel::ShowView);

  YAxisPanel* y_axis = oscilloscope_panel_->y_axis_panel();
  connect(y_axis, &YAxisPanel::MeasurementVisibilityChanged, &waveform_widget_,
          &WaveformWidget::SetMeasurementVisible);
  connect(y_axis, &YAxisPanel::ScaleChanged, &waveform_widget_,
          &WaveformWidget::SetScale);
  connect(y_axis, &YAxisPanel::CenterChanged, &waveform_widget_,
          &WaveformWidget::SetCenter);
  connect(&waveform_widget_, &WaveformWidget::ViewChanged, y_axis,
          &YAxisPanel::ShowView);
  connect(oscilloscope_panel_, &OscilloscopePanel::SettingsChanged,
          &waveform_widget_, &WaveformWidget::Refresh);
}

void RealtimePanel::Activate() {
  if (saved_setup_) {
    for (int ch = 0; ch < core::m1k::kChannelCount; ++ch) {
      device_.SetGeneratorConfig(ch, saved_setup_->generators[ch]);
    }
    device_.SetTrigger(saved_setup_->trigger);
    device_.SetTimeWindow(saved_setup_->time_window);
  }
  // Notified on the acquisition thread; process the frame on the GUI thread.
  WaveformWidget* waveform_widget = &waveform_widget_;
  device_.SetFrameReadyCallback([waveform_widget] {
    QMetaObject::invokeMethod(waveform_widget, &WaveformWidget::Refresh,
                              Qt::QueuedConnection);
  });
  if (runs_when_active_) StartAcquisition();
}

void RealtimePanel::Deactivate() {
  runs_when_active_ = device_.IsRunning();
  device_.StopAcquisition();
  UpdatePauseButton();
  DeviceSetup& setup = saved_setup_.emplace();
  for (int ch = 0; ch < core::m1k::kChannelCount; ++ch) {
    setup.generators[ch] = device_.GetGeneratorConfig(ch);
  }
  setup.trigger = device_.Trigger();
  setup.time_window = device_.TimeWindow();
}

void RealtimePanel::TogglePause() {
  if (device_.IsRunning()) {
    device_.StopAcquisition();
    UpdatePauseButton();
  } else {
    StartAcquisition();
  }
}

void RealtimePanel::StartAcquisition() {
  if (!device_.StartAcquisition()) ShowStartFailure(this);
  UpdatePauseButton();
}

void RealtimePanel::UpdatePauseButton() {
  btn_pause_->setText(device_.IsRunning() ? "Pause" : "Resume");
}

}  // namespace m1k_scope::ui
