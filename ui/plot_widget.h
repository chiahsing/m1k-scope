#pragma once

#include <QColor>
#include <QMargins>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QWidget>
#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <vector>

#include "core/measurement.h"

namespace m1k_scope::ui {

// Number of horizontal and vertical divisions of the waveform display.
constexpr int kNumDivisionsX = 10;
constexpr int kNumDivisionsY = 10;

// Times per division the time axis zooms through, a 1-2-5 sequence. Each one
// sets the time window of the Device to kNumDivisionsX divisions.
std::span<const std::chrono::microseconds> GetTimesPerDivision();

// Index into GetTimesPerDivision() of the time per division on startup and
// after a zoom reset.
int GetDefaultTimeScaleIndex();

// Returns the time window at GetTimesPerDivision()[index]: kNumDivisionsX
// divisions.
std::chrono::microseconds GetTimeWindow(int index);

// Formats GetTimesPerDivision()[index], e.g. "5 ms/div".
QString FormatTimePerDivision(int index);

// Formats GetTimeWindow(index), e.g. "50 ms".
QString FormatTimeWindow(int index);

// Returns the time in ms at fraction of the plot width, from 0 at its left
// edge to 1 at its right, at GetTimesPerDivision()[index]. 0 ms is at the
// center, where a trigger point is shown.
double GetTimeAt(int index, double fraction);

// Formats a time label of the time axis, in ms, e.g. "-25 ms".
QString FormatTimeLabel(double ms);

// Formats a time read out at the cursor, in ms, down to one sample, e.g.
// "1.36 ms".
QString FormatTimeReadout(double ms);

// Direction of an axis of the waveform display: the bottom one, or one on a
// side.
enum class Orientation { kHorizontal, kVertical };

// Both orientations, for iterating over them.
constexpr Orientation kOrientations[] = {Orientation::kHorizontal,
                                         Orientation::kVertical};

// Number of divisions along an axis of orientation.
constexpr int GetNumDivisions(Orientation orientation) {
  return orientation == Orientation::kHorizontal ? kNumDivisionsX
                                                 : kNumDivisionsY;
}

// The axis of one quantity, labeled in a shown unit: a vertical axis of the
// waveform display, voltage on the left and current on the right, or its
// bottom axis while it plots against a measurement of the quantity.
struct Axis {
  core::Quantity quantity;
  double unit_scale;  // Shown unit per unit of the quantity
  const char* unit;   // Shown unit
  // Values per division the axis zooms through, in the quantity's unit, a
  // 1-2-5 sequence. The last one spans the whole range the M1K measures and
  // is the default.
  std::span<const double> per_division;
  // Decimals of a value read out at the cursor, in the shown unit: about the
  // resolution of the M1K's 16-bit ADC over the range.
  int readout_decimals;
};

// Both quantities with an axis, for iterating over the axes.
constexpr core::Quantity kAxisQuantities[] = {core::Quantity::kVoltage,
                                              core::Quantity::kCurrent};

// Axis of quantity.
const Axis& GetAxis(core::Quantity quantity);

// Index into axis.per_division of the default scale: the whole range.
int GetDefaultScaleIndex(const Axis& axis);

// Value at the center of the view by default: the middle of the range.
double GetDefaultCenter(const Axis& axis);

// Formats per_division[index] of axis, e.g. "0.5 V/div".
QString FormatPerDivision(const Axis& axis, int index);

// Colors and sizes of the plots.
inline constexpr QColor kAxisColor{60, 80, 60};      // Axis lines, border
inline constexpr QColor kGridColor{40, 50, 40};      // Dotted division lines
inline constexpr QColor kLabelColor{160, 170, 160};  // Labels, readout
inline constexpr int kTraceWidth = 1;                // In pixels
inline constexpr int kLabelGap = 4;  // Between a plot area and its labels

// A line of a cursor readout.
struct ReadoutLine {
  QString text;
  QColor color;
};

// Base of all plots: a black widget, expanding with the window, with a cursor
// readout. While the cursor is over GetCursorArea(), a dashed line marks it and
// a box beside it reads out what GetReadout() returns there.
class PlotCanvas : public QWidget {
  Q_OBJECT

 public:
  ~PlotCanvas() override = default;

  // Returns the recommended size of the plot.
  QSize sizeHint() const override;

  // Returns the minimum size keeping the divisions and labels readable.
  QSize minimumSizeHint() const override;

 protected:
  // Constructs the plot, at least minimum_size large.
  PlotCanvas(QSize minimum_size, QWidget* parent);

  // Draws the plot over the black background.
  virtual void DrawPlot(QPainter& painter) = 0;

  // Returns the area the cursor readout follows the cursor over.
  virtual QRect GetCursorArea() const = 0;

  // Returns the readout at x, in pixels from the left edge of a cursor area
  // width pixels wide.
  virtual std::vector<ReadoutLine> GetReadout(double x, int width) const = 0;

  // Leaves room around the plot for the labels, which depend on the font.
  // Called again whenever the font changes; subclasses call it once when
  // constructed.
  virtual void UpdatePlotMargins() = 0;

  // Draws text centered at center, shifted inside bounds if it would stick out.
  static void DrawLabel(QPainter& painter, const QString& text, QPoint center,
                        const QRect& bounds);

  void changeEvent(QEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  void DrawCursor(QPainter& painter) const;

  void paintEvent(QPaintEvent* event) override;

  const QSize minimum_size_;

  // Position of the cursor while it is over the cursor area.
  std::optional<QPoint> cursor_;
};

// Base of the plots against measurements: a plot area with a grid of
// kNumDivisionsX by kNumDivisionsY divisions and labeled axes around it.
//
// Voltage is on the left axis and current on the right one; the subclass
// provides the bottom axis through GetBottomLabel(). Each quantity has a view
// on each orientation of axis, set with SetView(): a value per division from
// its Axis and the value at the center, within the range the M1K measures. The
// views on the bottom axis are for a subclass plotting against a measurement.
// A line marks 0 where it is inside a range, such as 0 mA.
class PlotWidget : public PlotCanvas {
  Q_OBJECT

 public:
  ~PlotWidget() override = default;

 protected:
  // An area of labels beside the plot: of the left or right vertical axis, or
  // of the bottom axis.
  enum class AxisArea { kLeft, kRight, kBottom };

  // View of an axis.
  struct AxisView {
    int scale_index = 0;  // Index into its Axis::per_division
    double center = 0;    // Value at the center of the plot, in its unit
  };

  // Constructs the plot with all axes at their default views.
  explicit PlotWidget(QWidget* parent);

  // Draws the data in the plot area of size, with the origin at its top-left.
  virtual void DrawData(QPainter& painter, const QSize& size) = 0;

  // Returns the label of vertical grid line i, counted from the left.
  virtual QString GetBottomLabel(int i) const = 0;

  // Returns the area of the plot within the widget, inside the labels.
  QRect GetPlotRect() const;

  // Returns where pos is in the plot, as fractions of its width from the left
  // edge and of its height from the bottom edge, within 0 to 1, the way GetX()
  // and GetY() map values.
  QPointF GetPlotFraction(const QPointF& pos) const;

  // Returns the view of quantity on the axis of orientation.
  const AxisView& GetView(Orientation orientation,
                          core::Quantity quantity) const;

  // Sets the view of quantity on the axis of orientation, keeping it within
  // the range the M1K measures, and repaints.
  void SetView(Orientation orientation, core::Quantity quantity,
               int scale_index, double center);

  // Returns the value per division, and the span, of the view of quantity on
  // the axis of orientation, in its unit.
  double GetPerDivision(Orientation orientation, core::Quantity quantity) const;
  double GetViewSpan(Orientation orientation, core::Quantity quantity) const;

  // Returns the value of quantity on the axis of orientation at fraction of
  // the plot, from 0 at its left or bottom edge to 1 at its right or top.
  double GetViewValue(Orientation orientation, core::Quantity quantity,
                      double fraction) const;

  // Returns the x of value of quantity on the bottom axis, in a plot width
  // pixels wide.
  double GetX(double value, core::Quantity quantity, int width) const;

  // Returns the y of value of quantity on a vertical axis, in a plot height
  // pixels tall.
  double GetY(double value, core::Quantity quantity, int height) const;

  // Returns the area of axis labels at pos, if any.
  std::optional<AxisArea> GetAxisAreaAt(const QPoint& pos) const;

  // Returns the quantity of the vertical axis of area, kLeft or kRight.
  static core::Quantity GetSideQuantity(AxisArea area);

  // Highlights the labels of area, or none for nullopt, and repaints.
  void SetHighlightedAxis(std::optional<AxisArea> area);

  QRect GetCursorArea() const override;

  // Formats value of quantity for the readout, in the axis' shown unit.
  static QString FormatReadout(double value, core::Quantity quantity);

  // Formats value of quantity for a label of an axis at division per
  // division, in the axis' shown unit.
  static QString FormatAxisLabel(double value, core::Quantity quantity,
                                 double division);

 private:
  double GetFraction(Orientation orientation, core::Quantity quantity,
                     double value) const;
  QString GetAxisLabel(core::Quantity quantity, int i) const;
  QRect GetAxisAreaRect(AxisArea area) const;
  void DrawGrid(QPainter& painter, const QSize& size);
  void DrawLabels(QPainter& painter, const QRect& plot);

  void DrawPlot(QPainter& painter) override;
  void UpdatePlotMargins() override;

  // Indexed by Orientation, then by core::Quantity.
  std::array<std::array<AxisView, std::size(kAxisQuantities)>,
             std::size(kOrientations)>
      views_;

  // Room left around the plot for the axis labels.
  QMargins plot_margins_;

  // Area of axis labels highlighted, if any.
  std::optional<AxisArea> highlighted_axis_;
};

}  // namespace m1k_scope::ui
