#include "plot_widget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <iterator>

#include "common.h"

namespace m1k_scope::ui {

namespace {

using std::chrono_literals::operator""us;
using std::chrono_literals::operator""ms;
// Up to 10 ms/div, so the time axis spans at most -50 ms to 50 ms.
constexpr std::chrono::microseconds kTimesPerDivision[] = {
    100us, 200us, 500us, 1ms, 2ms, 5ms, 10ms,
};
constexpr std::chrono::microseconds kDefaultTimePerDivision = 5ms;
static_assert(std::ranges::find(kTimesPerDivision, kDefaultTimePerDivision) !=
              std::end(kTimesPerDivision));

// Decimals of a time read out at the cursor, in ms: down to one sample.
const int kTimeReadoutDecimals =
    int(std::ceil(std::log10(core::m1k::kSampleRate / 1000.0)));

// Returns GetTimesPerDivision()[index] in milliseconds.
double GetMsPerDivision(int index) {
  return std::chrono::duration<double, std::milli>(kTimesPerDivision[index])
      .count();
}

constexpr double kVoltsPerDivision[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5};
constexpr double kAmpsPerDivision[] = {0.0001, 0.0002, 0.0005, 0.001, 0.002,
                                       0.005,  0.01,   0.02,   0.04};

// Resolutions are about 0.08 mV over 5 V and 6 uA over 400 mA.
constexpr Axis kVoltageAxis{core::Quantity::kVoltage, 1.0, "V",
                            kVoltsPerDivision, 4};
constexpr Axis kCurrentAxis{core::Quantity::kCurrent, kMaPerAmpere, "mA",
                            kAmpsPerDivision, 3};

// Decimals of a label at a division of division: one more than the division
// needs, as the view may start at any value.
int GetDecimals(double division) {
  return std::max(0, int(std::ceil(-std::log10(division)))) + 1;
}

// Formats a label of axis at value, in the quantity's unit.
QString FormatLabel(const Axis& axis, double value, int decimals) {
  return QString::number(value * axis.unit_scale, 'f', decimals) + " " +
         axis.unit;
}

// Width of the widest label of axis at any zoom, so the plot does not move
// when zooming.
int GetLabelWidth(const QFontMetrics& metrics, const Axis& axis) {
  const int decimals = GetDecimals(axis.per_division[0] * axis.unit_scale);
  int width = 0;
  for (const double value :
       {core::GetMinValue(axis.quantity), core::GetMaxValue(axis.quantity)}) {
    width = std::max(
        width, metrics.horizontalAdvance(FormatLabel(axis, value, decimals)));
  }
  return width;
}

// Height of the labels of the bottom axis, in lines of label text, roomier
// than the labels need so it is easy to point at for scrolling.
constexpr int kBottomAxisLines = 2;

// Quantities of the left and right axes.
constexpr core::Quantity kLeftQuantity = core::Quantity::kVoltage;
constexpr core::Quantity kRightQuantity = core::Quantity::kCurrent;

// Highlight of the labels of an axis: a background behind them, and a
// brighter color.
constexpr QColor kAxisHighlightBackground(30, 40, 30);
constexpr QColor kAxisHighlightLabelColor(230, 240, 230);

// Recommended size of a plot, and the minimum of the waveform display, which
// keeps the divisions and their labels readable.
constexpr QSize kPreferredSize(800, 600);
constexpr QSize kPlotWidgetMinimumSize(500, 300);

// Cursor readout: a dashed line at the cursor and a box beside it.
constexpr QColor kCursorLineColor(200, 210, 200);
constexpr QColor kReadoutBackground(0, 0, 0, 200);  // Translucent black
constexpr int kReadoutOffset = 12;  // From the cursor to the box, in pixels
constexpr int kReadoutPadding = 4;  // Inside the box, in pixels

}  // namespace

std::span<const std::chrono::microseconds> GetTimesPerDivision() {
  return kTimesPerDivision;
}

int GetDefaultTimeScaleIndex() {
  return static_cast<int>(
      std::ranges::find(kTimesPerDivision, kDefaultTimePerDivision) -
      std::begin(kTimesPerDivision));
}

std::chrono::microseconds GetTimeWindow(int index) {
  return kTimesPerDivision[index] * kNumDivisionsX;
}

QString FormatTimePerDivision(int index) {
  return QString("%1 ms/div").arg(GetMsPerDivision(index));
}

QString FormatTimeWindow(int index) {
  return FormatTimeLabel(GetMsPerDivision(index) * kNumDivisionsX);
}

double GetTimeAt(int index, double fraction) {
  return (fraction - 0.5) * kNumDivisionsX * GetMsPerDivision(index);
}

QString FormatTimeLabel(double ms) {
  return QString::number(ms, 'g', 4) + " ms";
}

QString FormatTimeReadout(double ms) {
  return QString::number(ms, 'f', kTimeReadoutDecimals) + " ms";
}

const Axis& GetAxis(core::Quantity quantity) {
  return quantity == core::Quantity::kCurrent ? kCurrentAxis : kVoltageAxis;
}

int GetDefaultScaleIndex(const Axis& axis) {
  return static_cast<int>(axis.per_division.size()) - 1;
}

double GetDefaultCenter(const Axis& axis) {
  return (core::GetMinValue(axis.quantity) + core::GetMaxValue(axis.quantity)) /
         2;
}

QString FormatPerDivision(const Axis& axis, int index) {
  return QString("%1 %2/div")
      .arg(axis.per_division[index] * axis.unit_scale)
      .arg(axis.unit);
}

PlotCanvas::PlotCanvas(QSize minimum_size, QWidget* parent)
    : QWidget(parent), minimum_size_(minimum_size) {
  // A black background, also between repaints.
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::black);
  setPalette(pal);

  // Expand to fill the space the window leaves.
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Follow the cursor without a button pressed, for the cursor readout.
  setMouseTracking(true);
}

QSize PlotCanvas::sizeHint() const { return kPreferredSize; }

QSize PlotCanvas::minimumSizeHint() const { return minimum_size_; }

void PlotCanvas::DrawLabel(QPainter& painter, const QString& text,
                           QPoint center, const QRect& bounds) {
  QRect box = painter.fontMetrics().boundingRect(text);
  box.moveCenter(center);
  box.moveLeft(
      std::clamp(box.left(), bounds.left(), bounds.right() - box.width() + 1));
  box.moveTop(
      std::clamp(box.top(), bounds.top(), bounds.bottom() - box.height() + 1));
  painter.drawText(box, Qt::AlignCenter, text);
}

void PlotCanvas::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::FontChange) UpdatePlotMargins();
}

void PlotCanvas::mouseMoveEvent(QMouseEvent* event) {
  const QPoint pos = event->position().toPoint();
  if (GetCursorArea().contains(pos)) {
    cursor_ = pos;
  } else {
    cursor_.reset();
  }
  update();
}

void PlotCanvas::leaveEvent(QEvent* /*event*/) {
  cursor_.reset();
  update();
}

void PlotCanvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), Qt::black);
  DrawPlot(painter);
  // The readout box may reach past the plot, over the labels.
  DrawCursor(painter);
}

// Draws a dashed line across the cursor area at the cursor, and a box beside
// the cursor with the readout there, kept inside the widget.
void PlotCanvas::DrawCursor(QPainter& painter) const {
  if (!cursor_) return;
  const QRect area = GetCursorArea();
  // At the center of the cursor's pixel.
  const double x = cursor_->x() + 0.5;
  painter.setPen(QPen(kCursorLineColor, 1, Qt::DashLine));
  painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom() + 1));
  const std::vector<ReadoutLine> lines =
      GetReadout(x - area.left(), area.width());
  if (lines.empty()) return;

  // Box below and right of the cursor, flipped to the other side where it
  // would leave the widget.
  const QFontMetrics metrics = painter.fontMetrics();
  int text_width = 0;
  for (const ReadoutLine& line : lines) {
    text_width = std::max(text_width, metrics.horizontalAdvance(line.text));
  }
  QRect box(*cursor_ + QPoint(kReadoutOffset, kReadoutOffset),
            QSize(text_width + 2 * kReadoutPadding,
                  int(lines.size()) * metrics.height() + 2 * kReadoutPadding));
  if (box.right() > rect().right()) {
    box.moveRight(cursor_->x() - kReadoutOffset);
  }
  if (box.bottom() > rect().bottom()) {
    box.moveBottom(cursor_->y() - kReadoutOffset);
  }
  painter.fillRect(box, kReadoutBackground);
  painter.setPen(kAxisColor);
  painter.drawRect(box);
  int y = box.top() + kReadoutPadding;
  for (const ReadoutLine& line : lines) {
    painter.setPen(line.color);
    painter.drawText(
        QRect(box.left() + kReadoutPadding, y, text_width, metrics.height()),
        Qt::AlignLeft | Qt::AlignVCenter, line.text);
    y += metrics.height();
  }
}

PlotWidget::PlotWidget(QWidget* parent)
    : PlotCanvas(kPlotWidgetMinimumSize, parent) {
  UpdatePlotMargins();
  for (auto& views : views_) {
    for (const core::Quantity quantity : kAxisQuantities) {
      const Axis& axis = GetAxis(quantity);
      views[static_cast<int>(quantity)] = {GetDefaultScaleIndex(axis),
                                           GetDefaultCenter(axis)};
    }
  }
}

void PlotWidget::SetView(Orientation orientation, core::Quantity quantity,
                         int scale_index, double center) {
  const Axis& axis = GetAxis(quantity);
  AxisView& view =
      views_[static_cast<int>(orientation)][static_cast<int>(quantity)];
  view.scale_index =
      std::clamp(scale_index, 0, int(axis.per_division.size()) - 1);
  const double half_span = GetViewSpan(orientation, quantity) / 2;
  const double low = core::GetMinValue(quantity) + half_span;
  const double high = core::GetMaxValue(quantity) - half_span;
  // At the whole range, low and high may cross by a rounding error.
  view.center =
      low < high ? std::clamp(center, low, high) : GetDefaultCenter(axis);
  update();
}

const PlotWidget::AxisView& PlotWidget::GetView(Orientation orientation,
                                                core::Quantity quantity) const {
  return views_[static_cast<int>(orientation)][static_cast<int>(quantity)];
}

double PlotWidget::GetPerDivision(Orientation orientation,
                                  core::Quantity quantity) const {
  return GetAxis(quantity)
      .per_division[GetView(orientation, quantity).scale_index];
}

double PlotWidget::GetViewSpan(Orientation orientation,
                               core::Quantity quantity) const {
  return GetPerDivision(orientation, quantity) * GetNumDivisions(orientation);
}

// Returns the fraction of the plot at value of quantity on the axis of
// orientation, from 0 at its left or bottom edge to 1 at its right or top.
double PlotWidget::GetFraction(Orientation orientation, core::Quantity quantity,
                               double value) const {
  return (value - GetView(orientation, quantity).center) /
             GetViewSpan(orientation, quantity) +
         0.5;
}

double PlotWidget::GetViewValue(Orientation orientation,
                                core::Quantity quantity,
                                double fraction) const {
  return GetView(orientation, quantity).center +
         (fraction - 0.5) * GetViewSpan(orientation, quantity);
}

double PlotWidget::GetX(double value, core::Quantity quantity,
                        int width) const {
  return GetFraction(Orientation::kHorizontal, quantity, value) * width;
}

double PlotWidget::GetY(double value, core::Quantity quantity,
                        int height) const {
  // The plot spans the view from the bottom to the top edge.
  return height - GetFraction(Orientation::kVertical, quantity, value) * height;
}

QPointF PlotWidget::GetPlotFraction(const QPointF& pos) const {
  // The center of the pixel at pos.
  const QRect plot = GetPlotRect();
  return QPointF(
      std::clamp((pos.x() + 0.5 - plot.left()) / plot.width(), 0.0, 1.0),
      std::clamp((plot.bottom() + 0.5 - pos.y()) / plot.height(), 0.0, 1.0));
}

// Returns the rectangle of area: beside the plot as tall as it, or below it
// as wide as it.
QRect PlotWidget::GetAxisAreaRect(AxisArea area) const {
  const QRect plot = GetPlotRect();
  switch (area) {
    case AxisArea::kLeft:
      return QRect(0, plot.top(), plot.left(), plot.height());
    case AxisArea::kRight:
      return QRect(plot.right() + 1, plot.top(), width() - plot.right() - 1,
                   plot.height());
    case AxisArea::kBottom:
      return QRect(plot.left(), plot.bottom() + 1, plot.width(),
                   height() - plot.bottom() - 1);
  }
  return {};
}

std::optional<PlotWidget::AxisArea> PlotWidget::GetAxisAreaAt(
    const QPoint& pos) const {
  for (const AxisArea area :
       {AxisArea::kLeft, AxisArea::kRight, AxisArea::kBottom}) {
    if (GetAxisAreaRect(area).contains(pos)) return area;
  }
  return std::nullopt;
}

core::Quantity PlotWidget::GetSideQuantity(AxisArea area) {
  return area == AxisArea::kLeft ? kLeftQuantity : kRightQuantity;
}

void PlotWidget::SetHighlightedAxis(std::optional<AxisArea> area) {
  if (area == highlighted_axis_) return;
  highlighted_axis_ = area;
  update();
}

QString PlotWidget::GetAxisLabel(core::Quantity quantity, int i) const {
  // Grid line i of the vertical axis, counted from the bottom.
  return FormatAxisLabel(GetViewValue(Orientation::kVertical, quantity,
                                      double(i) / kNumDivisionsY),
                         quantity,
                         GetPerDivision(Orientation::kVertical, quantity));
}

QString PlotWidget::FormatReadout(double value, core::Quantity quantity) {
  const Axis& axis = GetAxis(quantity);
  return FormatLabel(axis, value, axis.readout_decimals);
}

QString PlotWidget::FormatAxisLabel(double value, core::Quantity quantity,
                                    double division) {
  const Axis& axis = GetAxis(quantity);
  return FormatLabel(axis, value, GetDecimals(division * axis.unit_scale));
}

void PlotWidget::UpdatePlotMargins() {
  // Leave room for the labels of the axes on the left, right and bottom.
  const QFontMetrics metrics = fontMetrics();
  auto side_width = [&metrics](core::Quantity quantity) {
    return GetLabelWidth(metrics, GetAxis(quantity)) + 2 * kLabelGap;
  };
  plot_margins_ =
      QMargins(side_width(kLeftQuantity), 0, side_width(kRightQuantity),
               kBottomAxisLines * metrics.height() + 2 * kLabelGap);
}

QRect PlotWidget::GetPlotRect() const {
  return rect().marginsRemoved(plot_margins_);
}

QRect PlotWidget::GetCursorArea() const { return GetPlotRect(); }

void PlotWidget::DrawPlot(QPainter& painter) {
  const QRect plot = GetPlotRect();
  DrawLabels(painter, plot);

  // Draw the grid and the data inside the plot area.
  painter.save();
  painter.translate(plot.topLeft());
  painter.setClipRect(QRect(QPoint(0, 0), plot.size()));
  DrawGrid(painter, plot.size());
  DrawData(painter, plot.size());
  painter.restore();
}

void PlotWidget::DrawGrid(QPainter& painter, const QSize& size) {
  const int w = size.width();
  const int h = size.height();

  // Dim dotted division lines.
  painter.setPen(QPen(kGridColor, 1, Qt::DotLine));
  for (int i = 1; i < kNumDivisionsX; ++i) {
    const int x = (w * i) / kNumDivisionsX;
    painter.drawLine(x, 0, x, h);
  }
  for (int i = 1; i < kNumDivisionsY; ++i) {
    const int y = (h * i) / kNumDivisionsY;
    painter.drawLine(0, y, w, y);
  }

  // Brighter solid lines: 0 of the axes whose range it is inside, and the
  // border.
  painter.setPen(QPen(kAxisColor, 1, Qt::SolidLine));
  for (const core::Quantity quantity : kAxisQuantities) {
    if (core::GetMinValue(quantity) >= 0 || core::GetMaxValue(quantity) <= 0) {
      continue;
    }
    const double y = GetY(0, quantity, h);
    painter.drawLine(QPointF(0, y), QPointF(w, y));
  }
  painter.drawRect(0, 0, w - 1, h - 1);
}

void PlotWidget::DrawLabels(QPainter& painter, const QRect& plot) {
  if (highlighted_axis_) {
    painter.fillRect(GetAxisAreaRect(*highlighted_axis_),
                     kAxisHighlightBackground);
  }
  auto set_label_pen = [this, &painter](AxisArea area) {
    painter.setPen(area == highlighted_axis_ ? kAxisHighlightLabelColor
                                             : kLabelColor);
  };

  // Below each vertical grid line, halfway down the bottom axis. The end
  // labels may reach into the empty corners below the side labels, so they
  // stay centered on their grid lines instead of being pushed into their
  // neighbors; only the widget edges bound them.
  const int bottom_y = GetAxisAreaRect(AxisArea::kBottom).center().y();
  set_label_pen(AxisArea::kBottom);
  for (int i = 0; i <= kNumDivisionsX; ++i) {
    const int x = plot.left() + plot.width() * i / kNumDivisionsX;
    DrawLabel(painter, GetBottomLabel(i), QPoint(x, bottom_y), rect());
  }

  // Beside each horizontal grid line, on the left and right.
  const QRect side_bounds(0, plot.top(), width(), plot.height());
  for (const core::Quantity quantity : kAxisQuantities) {
    const bool left = quantity == kLeftQuantity;
    const int x = left ? plot.left() / 2 : (plot.right() + width()) / 2;
    set_label_pen(left ? AxisArea::kLeft : AxisArea::kRight);
    for (int i = 0; i <= kNumDivisionsY; ++i) {
      const int y = plot.bottom() - plot.height() * i / kNumDivisionsY;
      DrawLabel(painter, GetAxisLabel(quantity, i), QPoint(x, y), side_bounds);
    }
  }
}

}  // namespace m1k_scope::ui
