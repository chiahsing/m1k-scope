#include "waveform_widget.h"

#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <optional>

#include "common.h"
#include "core/device.h"

namespace m1k_scope::ui {

namespace {

// Color of the trigger level and time lines.
constexpr QColor kTriggerLineColor(128, 128, 128);

// Size of the reset zoom icon, and its inset from the plot's top-right corner,
// in pixels.
constexpr int kResetZoomIconSize = 16;
constexpr int kResetZoomInset = 4;

// Colors of the reset zoom icon: lit while the zoom differs from the default,
// so there is something to reset, and dim otherwise.
constexpr QColor kResetZoomLitColor(60, 255, 60);  // Bright green
constexpr QColor kResetZoomDimColor(70, 85, 70);

// "Fit to view" icon in color: the four corners of a square.
QPixmap GetResetZoomPixmap(const QColor& color) {
  QPixmap pixmap(kResetZoomIconSize, kResetZoomIconSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::SquareCap));
  constexpr int kLow = 2;
  constexpr int kHigh = kResetZoomIconSize - 2;
  constexpr int kArm = kResetZoomIconSize / 4;
  for (const int x : {kLow, kHigh}) {
    for (const int y : {kLow, kHigh}) {
      const int dx = x == kLow ? kArm : -kArm;
      const int dy = y == kLow ? kArm : -kArm;
      painter.drawLine(x, y, x + dx, y);
      painter.drawLine(x, y, x, y + dy);
    }
  }
  return pixmap;
}

// Reset zoom icon: lit while the button is enabled and dim while disabled.
QIcon GetResetZoomIcon() {
  QIcon icon(GetResetZoomPixmap(kResetZoomLitColor));
  icon.addPixmap(GetResetZoomPixmap(kResetZoomDimColor), QIcon::Disabled);
  return icon;
}

// Wheel rotation of one notch, in eighths of a degree.
constexpr int kWheelStep = 120;

// Adds delta to the wheel rotation in remainder and returns the whole notches
// in it, leaving the rest in remainder.
int TakeWheelSteps(int& remainder, int delta) {
  remainder += delta;
  const int steps = remainder / kWheelStep;
  remainder -= steps * kWheelStep;
  return steps;
}

}  // namespace

WaveformWidget::WaveformWidget(core::Device& device, QWidget* parent)
    : PlotWidget(parent), device_(device) {
  btn_reset_zoom_ = new QToolButton(this);
  btn_reset_zoom_->setIcon(GetResetZoomIcon());
  btn_reset_zoom_->setIconSize(QSize(kResetZoomIconSize, kResetZoomIconSize));
  btn_reset_zoom_->setAutoRaise(true);
  btn_reset_zoom_->setToolTip("Reset Zoom");
  btn_reset_zoom_->setCursor(Qt::ArrowCursor);
  connect(btn_reset_zoom_, &QToolButton::clicked, this,
          &WaveformWidget::ResetZoom);

  for (int m = 0; m < core::kMeasurementCount; ++m) {
    visible_[m] = IsShownByDefault(m);
  }
  ResetZoom();
}

void WaveformWidget::Refresh() {
  if (const auto frame = device_.ProcessLatestFrame(traces_)) {
    frame_window_ = frame->window;
  }
  update();
}

void WaveformWidget::ResetZoom() {
  for (const Orientation orientation : kOrientations) {
    for (const core::Quantity quantity : kAxisQuantities) {
      const Axis& axis = GetAxis(quantity);
      SetAxisView(orientation, quantity, GetDefaultScaleIndex(axis),
                  GetDefaultCenter(axis));
    }
  }
  SetTimeScale(GetDefaultTimeScaleIndex());
}

void WaveformWidget::SetScale(Orientation orientation, core::Quantity quantity,
                              int scale_index) {
  SetAxisView(orientation, quantity, scale_index,
              GetView(orientation, quantity).center);
  UpdateZoomDisplay();
}

void WaveformWidget::SetCenter(Orientation orientation, core::Quantity quantity,
                               double center) {
  SetAxisView(orientation, quantity, GetView(orientation, quantity).scale_index,
              center);
  UpdateZoomDisplay();
}

// Sets the view of quantity on the axis of orientation and announces it.
void WaveformWidget::SetAxisView(Orientation orientation,
                                 core::Quantity quantity, int scale_index,
                                 double center) {
  SetView(orientation, quantity, scale_index, center);
  AnnounceView(orientation, quantity);
}

// Emits ViewChanged() for the view of quantity on the axis of orientation.
void WaveformWidget::AnnounceView(Orientation orientation,
                                  core::Quantity quantity) {
  const AxisView& view = GetView(orientation, quantity);
  emit ViewChanged(orientation, quantity, view.scale_index, view.center);
}

// Returns the quantity of the X measurement, or nullopt while the bottom axis
// is time.
std::optional<core::Quantity> WaveformWidget::GetXQuantity() const {
  if (!x_measurement_) return std::nullopt;
  return core::kMeasurements[*x_measurement_].quantity;
}

// Zooms quantity on the axis of orientation by steps through its values per
// division, out for positive steps and in for negative ones. The value at
// fraction of the plot along the axis stays in place unless that would take
// the view outside the range the M1K measures.
void WaveformWidget::ZoomAxis(Orientation orientation, core::Quantity quantity,
                              int steps, double fraction) {
  const Axis& axis = GetAxis(quantity);
  const int last = static_cast<int>(axis.per_division.size()) - 1;
  const int scale_index =
      std::clamp(GetView(orientation, quantity).scale_index + steps, 0, last);
  const double anchor = GetViewValue(orientation, quantity, fraction);
  const double span =
      axis.per_division[scale_index] * GetNumDivisions(orientation);
  SetAxisView(orientation, quantity, scale_index,
              anchor + (0.5 - fraction) * span);
}

// Zooms the bottom axis by steps like ZoomAxis(). The time stays centered on
// 0 ms.
void WaveformWidget::ZoomBottomAxis(int steps, double fraction) {
  if (const std::optional<core::Quantity> quantity = GetXQuantity()) {
    ZoomAxis(Orientation::kHorizontal, *quantity, steps, fraction);
  } else {
    SetTimeScale(time_scale_index_ + steps);
  }
}

// Moves the view of quantity on the axis of orientation by fraction of the
// plot along the axis.
void WaveformWidget::MoveView(Orientation orientation, core::Quantity quantity,
                              double fraction) {
  const AxisView& view = GetView(orientation, quantity);
  SetAxisView(orientation, quantity, view.scale_index,
              view.center + fraction * GetViewSpan(orientation, quantity));
}

// Moves the views with the mouse, by delta pixels: the vertical axes, and the
// bottom axis unless it is time.
void WaveformWidget::Pan(const QPointF& delta) {
  const QRect plot = GetPlotRect();
  for (const core::Quantity quantity : kAxisQuantities) {
    MoveView(Orientation::kVertical, quantity, delta.y() / plot.height());
  }
  if (const std::optional<core::Quantity> quantity = GetXQuantity()) {
    MoveView(Orientation::kHorizontal, *quantity, -delta.x() / plot.width());
  }
  UpdateZoomDisplay();
}

// Returns whether all axes are at their default zoom.
bool WaveformWidget::IsDefaultZoom() const {
  if (time_scale_index_ != GetDefaultTimeScaleIndex()) return false;
  for (const Orientation orientation : kOrientations) {
    for (const core::Quantity quantity : kAxisQuantities) {
      const Axis& axis = GetAxis(quantity);
      const AxisView& view = GetView(orientation, quantity);
      if (view.scale_index != GetDefaultScaleIndex(axis) ||
          view.center != GetDefaultCenter(axis)) {
        return false;
      }
    }
  }
  return true;
}

// Lights up the reset zoom button while there is a zoom to reset, and
// repaints.
void WaveformWidget::UpdateZoomDisplay() {
  btn_reset_zoom_->setEnabled(!IsDefaultZoom());
  update();
}

// Puts the reset zoom button in the top-right corner of the plot.
void WaveformWidget::PlaceResetZoomButton() {
  const QRect plot = GetPlotRect();
  btn_reset_zoom_->adjustSize();
  btn_reset_zoom_->move(
      plot.right() - kResetZoomInset - btn_reset_zoom_->width() + 1,
      plot.top() + kResetZoomInset);
}

void WaveformWidget::resizeEvent(QResizeEvent* event) {
  PlotWidget::resizeEvent(event);
  PlaceResetZoomButton();
}

void WaveformWidget::changeEvent(QEvent* event) {
  // The plot moves with the margins, which follow the font.
  PlotWidget::changeEvent(event);
  if (event->type() == QEvent::FontChange) PlaceResetZoomButton();
}

void WaveformWidget::SetTimeScale(int scale_index) {
  time_scale_index_ =
      std::clamp(scale_index, 0, int(GetTimesPerDivision().size()) - 1);
  device_.SetTimeWindow(GetTimeWindow(time_scale_index_));
  emit TimeScaleChanged(time_scale_index_);
  UpdateZoomDisplay();
}

QString WaveformWidget::GetBottomLabel(int i) const {
  const double fraction = double(i) / kNumDivisionsX;
  if (const std::optional<core::Quantity> quantity = GetXQuantity()) {
    return FormatAxisLabel(
        GetViewValue(Orientation::kHorizontal, *quantity, fraction), *quantity,
        GetPerDivision(Orientation::kHorizontal, *quantity));
  }
  // 0 ms is at the center, where a trigger point is shown.
  return FormatTimeLabel(GetTimeAt(time_scale_index_, fraction));
}

void WaveformWidget::SetMeasurementVisible(int m, bool visible) {
  visible_[m] = visible;
  update();
}

void WaveformWidget::SetXMeasurement(std::optional<int> m) {
  x_measurement_ = m;
  if (const std::optional<core::Quantity> quantity = GetXQuantity()) {
    AnnounceView(Orientation::kHorizontal, *quantity);
  }
  update();
}

void WaveformWidget::DrawData(QPainter& painter, const QSize& size) {
  if (!x_measurement_) {
    // The vertical center axis, at 0 ms.
    painter.setPen(QPen(kAxisColor, 1, Qt::SolidLine));
    painter.drawLine(QPointF(size.width() / 2.0, 0),
                     QPointF(size.width() / 2.0, size.height()));
    DrawTrigger(painter, size);
  }

  for (int m = 0; m < core::kMeasurementCount; ++m) {
    if (!IsTraced(m)) continue;
    const std::vector<float>& samples = traces_[m];
    const core::Quantity quantity = core::kMeasurements[m].quantity;
    const std::size_t count = samples.size();
    if (count < 2) continue;
    painter.setPen(QPen(GetMeasurementColor(m), kTraceWidth));

    QPolygonF trace;
    trace.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      trace.append(QPointF(GetSampleX(i, count, size.width()),
                           GetY(samples[i], quantity, size.height())));
    }
    painter.drawPolyline(trace);
  }
}

void WaveformWidget::DrawTrigger(QPainter& painter, const QSize& size) {
  const core::TriggerConfig& trigger = device_.Trigger();
  if (trigger.measurement == core::kNoTrigger) return;

  // The trigger point is at the center of the time window; the level is
  // mapped the same way as the trace of its measurement.
  const double y =
      GetY(trigger.level, core::kMeasurements[trigger.measurement].quantity,
           size.height());
  painter.setPen(QPen(kTriggerLineColor, 1, Qt::DashLine));
  painter.drawLine(QPointF(size.width() / 2.0, 0),
                   QPointF(size.width() / 2.0, size.height()));
  painter.drawLine(QPointF(0, y), QPointF(size.width(), y));
}

// Returns whether the trace of measurement m is drawn: it is shown, and not on
// the bottom axis.
bool WaveformWidget::IsTraced(int m) const {
  return visible_[m] && x_measurement_ != m;
}

// Returns the x of sample i of the count in each trace, in a plot width
// pixels wide: the value of the X measurement there, or in time the center of
// the part of the frame it averages.
double WaveformWidget::GetSampleX(std::size_t i, std::size_t count,
                                  int width) const {
  if (x_measurement_) {
    const int x = *x_measurement_;
    return GetX(traces_[x][i], core::kMeasurements[x].quantity, width);
  }
  return GetFrameX((i + 0.5) / count, width);
}

// Returns how many pixels the frame spans. It spans the whole plot unless it
// is from before a zoom. width is the plot width in pixels.
double WaveformWidget::GetFrameWidth(int width) const {
  return width * frame_window_.count() /
         double(GetTimeWindow(time_scale_index_).count());
}

// Returns the x of position in the frame, from 0 at its start to 1 at its end.
// The frame is centered on the plot. width is the plot width in pixels.
double WaveformWidget::GetFrameX(double position, int width) const {
  return width / 2.0 + (position - 0.5) * GetFrameWidth(width);
}

// Returns the index of the sample at x in a trace of count samples, if any.
// width is the plot width in pixels.
std::optional<std::size_t> WaveformWidget::GetSampleIndexAt(double x,
                                                            std::size_t count,
                                                            int width) const {
  // Sample i covers the part [i / count, (i + 1) / count) of the frame.
  if (count == 0) return std::nullopt;
  const double position = (x - width / 2.0) / GetFrameWidth(width) + 0.5;
  if (position < 0 || position >= 1) return std::nullopt;
  return static_cast<std::size_t>(position * count);
}

// Returns the index of the sample of measurement m nearest to value, if any.
std::optional<std::size_t> WaveformWidget::GetNearestSampleIndex(
    int m, double value) const {
  const std::vector<float>& samples = traces_[m];
  if (samples.empty()) return std::nullopt;
  const auto nearest = std::ranges::min_element(
      samples, {}, [value](float v) { return std::abs(v - value); });
  return nearest - samples.begin();
}

std::vector<ReadoutLine> WaveformWidget::GetReadout(double x, int width) const {
  // The time or the X measurement's value at x, then each shown trace's value
  // at the sample there.
  std::vector<ReadoutLine> lines;
  std::optional<std::size_t> index;
  if (x_measurement_) {
    const int m = *x_measurement_;
    const core::Quantity quantity = core::kMeasurements[m].quantity;
    const double value =
        GetViewValue(Orientation::kHorizontal, quantity, x / width);
    lines.push_back(
        {GetMeasurementName(m) + " = " + FormatReadout(value, quantity),
         kLabelColor});
    index = GetNearestSampleIndex(m, value);
  } else {
    lines.push_back(
        {"t = " + FormatTimeReadout(GetTimeAt(time_scale_index_, x / width)),
         kLabelColor});
    // All traces have as many samples.
    index = GetSampleIndexAt(x, traces_[0].size(), width);
  }
  if (!index) return lines;
  for (int m = 0; m < core::kMeasurementCount; ++m) {
    if (!IsTraced(m)) continue;
    lines.push_back(
        {GetMeasurementName(m) + ": " +
             FormatReadout(traces_[m][*index], core::kMeasurements[m].quantity),
         GetMeasurementColor(m)});
  }
  return lines;
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton ||
      !GetPlotRect().contains(event->position().toPoint())) {
    return;
  }
  drag_position_ = event->position();
  UpdateHover(event->position().toPoint());
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event) {
  PlotWidget::mouseMoveEvent(event);
  UpdateHover(event->position().toPoint());
  if (!drag_position_) return;
  const QPointF delta = event->position() - *drag_position_;
  drag_position_ = event->position();
  Pan(delta);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  drag_position_.reset();
  UpdateHover(event->position().toPoint());
}

void WaveformWidget::leaveEvent(QEvent* event) {
  PlotWidget::leaveEvent(event);
  SetHighlightedAxis(std::nullopt);
}

// Shows what the mouse at pos can do: over the labels of an axis, a resize
// cursor along it and the labels highlighted, as scrolling zooms that axis;
// over the plot, an open hand, as dragging pans it. While dragging, a closed
// hand and no highlight.
void WaveformWidget::UpdateHover(const QPoint& pos) {
  if (drag_position_) {
    setCursor(Qt::ClosedHandCursor);
    SetHighlightedAxis(std::nullopt);
    return;
  }
  const std::optional<AxisArea> area = GetAxisAreaAt(pos);
  SetHighlightedAxis(area);
  if (area) {
    setCursor(area == AxisArea::kBottom ? Qt::SizeHorCursor
                                        : Qt::SizeVerCursor);
  } else if (GetPlotRect().contains(pos)) {
    setCursor(Qt::OpenHandCursor);
  } else {
    unsetCursor();
  }
}

void WaveformWidget::wheelEvent(QWheelEvent* event) {
  event->accept();
  // Scrolling up gives positive steps and zooms out.
  const int steps = TakeWheelSteps(wheel_remainder_, event->angleDelta().y());
  if (steps == 0) return;
  const QPoint pos = event->position().toPoint();
  const QPointF fraction = GetPlotFraction(event->position());
  const std::optional<AxisArea> area = GetAxisAreaAt(pos);
  if (area == AxisArea::kBottom) {
    ZoomBottomAxis(steps, fraction.x());
  } else if (area) {
    ZoomAxis(Orientation::kVertical, GetSideQuantity(*area), steps,
             fraction.y());
  } else if (GetPlotRect().contains(pos)) {
    ZoomBottomAxis(steps, fraction.x());
    for (const core::Quantity quantity : kAxisQuantities) {
      ZoomAxis(Orientation::kVertical, quantity, steps, fraction.y());
    }
  }
  UpdateZoomDisplay();
}

}  // namespace m1k_scope::ui
