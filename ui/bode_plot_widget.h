#pragma once

#include <QMargins>
#include <QRect>
#include <span>
#include <vector>

#include "core/bode_sweep.h"
#include "plot_widget.h"

namespace m1k_scope::ui {

using core::BodePoint;

// Plot of a frequency response: gain on top and phase below, against
// frequency on a shared log axis, with a dotted line per 1-2-...-9 step of
// each decade.
//
// The frequency axis spans what SetFrequencyRange() sets. Each vertical axis
// zooms to fit the points: in as far as its values per division allow, with its
// top on a whole division.
//
// While the cursor is over the plot, a dashed line marks it and a box beside
// it reads out the frequency there, and the gain and phase of the point
// nearest to it.
class BodePlotWidget : public PlotCanvas {
  Q_OBJECT

 public:
  explicit BodePlotWidget(QWidget* parent = nullptr);
  ~BodePlotWidget() override = default;

  // Spans the frequency axis from low to high, in Hertz, and removes the
  // points, which may lie outside it.
  void SetFrequencyRange(double low, double high);

  // Adds point, of a higher frequency than those added before, and fits the
  // vertical axes to the points.
  void AddPoint(const BodePoint& point);

 private:
  // A vertical axis: the values per division it zooms through, its unit, and
  // its view, the value at the top and per division.
  struct Scale {
    std::span<const double> per_divisions;
    const char* unit;  // After a value, with a space if it needs one
    double top = 0;
    double per_division = 0;
  };

  void FitScale(Scale& scale, double BodePoint::* value);
  QRect GetGainRect() const;
  QRect GetPhaseRect() const;
  double GetX(double frequency, int width) const;
  double GetFrequencyAt(double x, int width) const;
  void DrawPane(QPainter& painter, const QRect& pane, const Scale& scale,
                double BodePoint::* value, const QColor& color);
  void DrawFrequencyLabels(QPainter& painter, const QRect& pane);

  void DrawPlot(QPainter& painter) override;
  QRect GetCursorArea() const override;
  std::vector<ReadoutLine> GetReadout(double x, int width) const override;
  void UpdatePlotMargins() override;

  // Span of the frequency axis, in Hertz.
  double low_frequency_;
  double high_frequency_;

  std::vector<BodePoint> points_;
  Scale gain_;
  Scale phase_;

  // Room left around the panes for the labels. It depends only on the font.
  QMargins plot_margins_;
};

}  // namespace m1k_scope::ui
