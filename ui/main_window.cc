#include "main_window.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>

#include "bode_panel.h"
#include "bode_plot_widget.h"
#include "common.h"
#include "core/device.h"
#include "realtime_panel.h"
#include "waveform_widget.h"

namespace m1k_scope::ui {

namespace {

// QSettings key for the normal (non-maximized) window size saved across
// sessions.
constexpr char kNormalSizeKey[] = "main_window/normal_size";

// Window size used on first launch.
constexpr QSize kDefaultSize(1150, 660);

constexpr Qt::WindowStates kExpandedStates =
    Qt::WindowMaximized | Qt::WindowFullScreen;

}  // namespace

MainWindow::MainWindow(core::Device& device, QWidget* parent)
    : QMainWindow(parent), device_(device) {
  setWindowTitle("M1K Scope");

  SetUpUi();
  RestoreWindowGeometry();
  ConnectSignals();

  // Start the shown mode once the event loop runs, so an acquisition failure
  // reports over the shown window.
  QTimer::singleShot(0, this, [this] { ShowMode(modes_->currentIndex()); });
}

MainWindow::~MainWindow() {
  // The callback refers to a plot destroyed with this window.
  device_.StopAcquisition();
  device_.SetFrameReadyCallback(nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  SaveWindowGeometry();
  QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  // On Wayland the new size arrives before the matching window state change
  // (in both directions), so only decide whether this is a normal size once
  // the pending events have been processed.
  QTimer::singleShot(0, this, [this] {
    if (!(windowState() & kExpandedStates)) {
      normal_size_ = size();
    }
  });
}

void MainWindow::SetUpUi() {
  auto* central_widget = new QWidget(this);
  setCentralWidget(central_widget);
  auto* layout = new QHBoxLayout(central_widget);
  SetPanelMargins(layout);

  // Left: the plot of the mode takes all remaining horizontal space.
  plots_ = new QStackedWidget(central_widget);
  layout->addWidget(plots_, 1);

  // Right: the controls of the mode, switched by vertical tabs on the right
  // edge.
  modes_ = new QTabWidget(central_widget);
  modes_->setTabPosition(QTabWidget::East);
  layout->addWidget(modes_, 0);

  auto* waveform_widget = new WaveformWidget(device_, plots_);
  plots_->addWidget(waveform_widget);
  modes_->addTab(new RealtimePanel(device_, *waveform_widget, modes_),
                 "Realtime");

  auto* bode_plot_widget = new BodePlotWidget(plots_);
  plots_->addWidget(bode_plot_widget);
  modes_->addTab(new BodePanel(device_, *bode_plot_widget, modes_), "Bode");
}

void MainWindow::RestoreWindowGeometry() {
  // Always start at the last normal size, even if the window was maximized
  // when closed. Qt clamps the size to the minimum size if needed.
  QSettings settings;
  resize(settings.value(kNormalSizeKey, kDefaultSize).toSize());
}

void MainWindow::SaveWindowGeometry() {
  QSettings settings;
  settings.setValue(kNormalSizeKey,
                    normal_size_.isValid() ? normal_size_ : size());
}

void MainWindow::ConnectSignals() {
  connect(modes_, &QTabWidget::currentChanged, this, &MainWindow::ShowMode);

  // Ctrl+W closes the program.
  connect(new QShortcut(Qt::CTRL | Qt::Key_W, this), &QShortcut::activated,
          this, &MainWindow::close);
}

// Hands the M1K over to the mode at index, and shows its plot.
void MainWindow::ShowMode(int index) {
  ModePanel* mode = GetMode(index);
  if (mode == active_mode_) return;
  if (active_mode_) active_mode_->Deactivate();
  active_mode_ = mode;
  active_mode_->Activate();
  plots_->setCurrentIndex(index);
}

ModePanel* MainWindow::GetMode(int index) const {
  return static_cast<ModePanel*>(modes_->widget(index));
}

}  // namespace m1k_scope::ui
