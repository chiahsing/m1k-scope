#pragma once

#include <QPointF>
#include <array>
#include <chrono>
#include <optional>

#include "core/measurement.h"
#include "plot_widget.h"

class QToolButton;

namespace m1k_scope::core {
class Device;
}  // namespace m1k_scope::core

namespace m1k_scope::ui {

// Plot of the M1K's measurements against time, or against one of them, as it
// acquires them.
//
// Voltage is on the left axis and current on the right one, with one
// color-coded trace per shown measurement. Refresh() takes each new frame from
// the Device.
//
// The bottom axis is time, with 0 ms at the center, unless SetXMeasurement()
// sets a measurement for it: then the plot is X-Y, each trace drawn against
// that measurement, whose own trace is not drawn, with a view separate from
// that of the vertical axis of its quantity. While a trigger is set and the
// bottom axis is time, gray dashed lines mark its level and time.
//
// The time zooms through a 1-2-5 sequence of times per division with
// SetTimeScale(), also in X-Y, where it sets how long a frame is; each zoom
// step sets the Device's time window, and the widget also sets the initial one
// when constructed. The views of the other axes are set with SetScale() and
// SetCenter().
//
// With the mouse, scrolling over the labels of an axis zooms it through its
// values per division, and scrolling over the plot zooms all axes, keeping the
// values under the cursor in place; the time stays centered on 0 ms.
// Dragging the plot moves the views with it, except the time. Hints show
// this under the mouse: the labels of an axis light up with a resize cursor
// along it, and the plot shows an open hand.
//
// ResetZoom(), or a button in the top-right corner of the plot, zooms all axes
// back to their defaults. The button lights up while any zoom differs from the
// default.
//
// The cursor readout shows the time and the value of each shown trace there,
// or in X-Y the value of the X measurement there and the value of each shown
// trace at the sample nearest to it.
class WaveformWidget : public PlotWidget {
  Q_OBJECT

 public:
  // Constructs the widget. device must outlive this widget.
  explicit WaveformWidget(core::Device& device, QWidget* parent = nullptr);
  ~WaveformWidget() override = default;

  // Refills the traces from a new frame, if one is ready, and repaints. Call
  // it after changing trigger settings too, so they show even while stopped.
  void Refresh();

  // Zooms all axes back to their defaults: the default time per division and
  // the whole ranges the M1K measures.
  void ResetZoom();

  // Sets the time per division to GetTimesPerDivision()[scale_index], which
  // sets the Device's time window.
  void SetTimeScale(int scale_index);

  // Sets the value per division of quantity on the axis of orientation to
  // GetAxis(quantity).per_division[scale_index], keeping its center.
  void SetScale(Orientation orientation, core::Quantity quantity,
                int scale_index);

  // Sets the value at the center of quantity on the axis of orientation, in
  // its unit.
  void SetCenter(Orientation orientation, core::Quantity quantity,
                 double center);

  // Shows or hides the trace of measurement m, an index into
  // core::kMeasurements. Traces start shown as IsShownByDefault() says.
  void SetMeasurementVisible(int m, bool visible);

  // Sets the measurement on the bottom axis, an index into
  // core::kMeasurements, or nullopt for time, as on startup.
  void SetXMeasurement(std::optional<int> m);

 signals:
  // Emitted whenever the view of quantity on the axis of orientation changes,
  // by any means, and for the bottom axis when it starts showing quantity.
  void ViewChanged(Orientation orientation, core::Quantity quantity,
                   int scale_index, double center);

  // Emitted whenever the time per division changes, by any means, as an index
  // into GetTimesPerDivision().
  void TimeScaleChanged(int scale_index);

 protected:
  void DrawData(QPainter& painter, const QSize& size) override;
  std::vector<ReadoutLine> GetReadout(double x, int width) const override;
  QString GetBottomLabel(int i) const override;

 private:
  void SetAxisView(Orientation orientation, core::Quantity quantity,
                   int scale_index, double center);
  void AnnounceView(Orientation orientation, core::Quantity quantity);
  std::optional<core::Quantity> GetXQuantity() const;
  void ZoomAxis(Orientation orientation, core::Quantity quantity, int steps,
                double fraction);
  void ZoomBottomAxis(int steps, double fraction);
  void MoveView(Orientation orientation, core::Quantity quantity,
                double fraction);
  void Pan(const QPointF& delta);
  void UpdateHover(const QPoint& pos);
  bool IsDefaultZoom() const;
  void PlaceResetZoomButton();
  void UpdateZoomDisplay();
  void DrawTrigger(QPainter& painter, const QSize& size);
  bool IsTraced(int m) const;
  double GetSampleX(std::size_t i, std::size_t count, int width) const;
  double GetFrameWidth(int width) const;
  double GetFrameX(double position, int width) const;
  std::optional<std::size_t> GetSampleIndexAt(double x, std::size_t count,
                                              int width) const;
  std::optional<std::size_t> GetNearestSampleIndex(int m, double value) const;

  void changeEvent(QEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  core::Device& device_;
  QToolButton* btn_reset_zoom_{nullptr};

  // Traces of the latest frame, refilled in place by Refresh().
  core::Traces traces_;

  // Whether the trace of each measurement is shown, indexed like traces_.
  std::array<bool, core::kMeasurementCount> visible_{};

  // The measurement on the bottom axis, or nullopt for time.
  std::optional<int> x_measurement_;

  // Time window of the frame of traces_. Traces are drawn to scale
  // against the Device's time window, which may differ after a zoom.
  std::chrono::microseconds frame_window_{0};

  // Time per division, an index into GetTimesPerDivision(). The Device's time
  // window follows it; see SetTimeScale().
  int time_scale_index_{0};

  // Wheel rotation not yet turned into zoom steps, in eighths of a degree.
  // Touchpads scroll in smaller increments than a wheel notch.
  int wheel_remainder_{0};

  // Where the mouse was last while dragging the plot.
  std::optional<QPointF> drag_position_;
};

}  // namespace m1k_scope::ui
