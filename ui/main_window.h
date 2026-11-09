#pragma once

#include <QMainWindow>
#include <QSize>
#include <QWidget>

class QStackedWidget;
class QTabWidget;

namespace m1k_scope::core {
class Device;
}  // namespace m1k_scope::core

namespace m1k_scope::ui {

// Base of the controls of a mode of the main window, such as Realtime or Bode.
//
// Only one mode uses the M1K at a time: the main window deactivates the mode
// it leaves before activating the one it shows. Activating a mode sets up the
// Device for it, and deactivating stops acquisition.
class ModePanel : public QWidget {
 public:
  ~ModePanel() override = default;

  // Sets up the Device for this mode, while acquisition is stopped: what it
  // generates and how it delivers frames.
  virtual void Activate() = 0;

  // Stops acquisition.
  virtual void Deactivate() = 0;

 protected:
  // Constructs the panel. device must outlive this panel.
  ModePanel(core::Device& device, QWidget* parent)
      : QWidget(parent), device_(device) {}

  core::Device& device_;
};

// Main application window.
//
// A plot on the left, expanding with the window, and the controls of the mode
// on the right. Vertical tabs on the right edge switch between the modes, and
// the plot switches with them:
//   - Realtime: WaveformWidget and RealtimePanel, the M1K acquiring
//     continuously.
//   - Bode: BodePlotWidget and BodePanel, measuring a frequency response.
// Only one mode uses the M1K at a time; see ModePanel.
//
// The window minimum size is derived from the layouts so the controls are never
// squished. The normal window size is restored across sessions.
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  // Constructs the main window. device must outlive this window.
  explicit MainWindow(core::Device& device, QWidget* parent = nullptr);
  ~MainWindow() override;

 protected:
  // Saves the normal window size so it can be restored on the next launch.
  void closeEvent(QCloseEvent* event) override;

  // Tracks the normal (non-maximized) window size.
  void resizeEvent(QResizeEvent* event) override;

 private:
  void SetUpUi();
  void RestoreWindowGeometry();
  void SaveWindowGeometry();
  void ConnectSignals();
  void ShowMode(int index);
  ModePanel* GetMode(int index) const;

  core::Device& device_;

  // Plots and controls of the modes, at the same indices.
  QStackedWidget* plots_{nullptr};
  QTabWidget* modes_{nullptr};  // Each tab is a ModePanel

  // The mode using the M1K, the shown one once the window is set up.
  ModePanel* active_mode_{nullptr};

  // Normal (non-maximized) window size, tracked manually because Qt's
  // normalGeometry() is unreliable on Wayland: the resize arrives before the
  // matching state change.
  QSize normal_size_;
};

}  // namespace m1k_scope::ui
