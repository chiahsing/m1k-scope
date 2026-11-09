#include "bode_plot_widget.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace m1k_scope::ui {

namespace {

// Divisions of each pane, and the values per division each vertical axis
// zooms through.
constexpr int kNumDivisions = 8;
constexpr double kGainsPerDivision[] = {1, 2, 5, 10, 20};     // In dB
constexpr double kPhasesPerDivision[] = {5, 10, 15, 30, 45};  // In degrees

// Views of the vertical axes before there are points: 0 to -80 dB, and
// 180 to -180 degrees.
constexpr double kDefaultGainTop = 0;
constexpr double kDefaultGainPerDivision = 10;
constexpr double kDefaultPhaseTop = 180;
constexpr double kDefaultPhasePerDivision = 45;

// Span of the frequency axis before SetFrequencyRange() sets one, in Hertz.
constexpr double kDefaultLowFrequency = 10;
constexpr double kDefaultHighFrequency = 10000;

// Hertz per kHz, for labels of 1 kHz and up.
constexpr double kHertzPerKilohertz = 1000;

// Steps of a decade marked by a grid line, as multiples of its start.
constexpr int kStepsPerDecade = 9;

// Colors of the gain and phase curves, and of the grid lines inside a decade.
constexpr QColor kGainColor(0, 200, 255);   // Cyan
constexpr QColor kPhaseColor(255, 215, 0);  // Yellow
constexpr QColor kMinorGridColor(28, 35, 28);

// Radius of the dot at each point, in pixels.
constexpr double kPointRadius = 2;

// Minimum size of the plot, keeping both panes readable.
constexpr QSize kMinimumSize(500, 400);

// Formats frequency, in Hertz, as "200 Hz" or "1.5 kHz", with up to digits
// significant digits.
QString FormatFrequency(double frequency, int digits) {
  return frequency < kHertzPerKilohertz
             ? QString::number(frequency, 'g', digits) + " Hz"
             : QString::number(frequency / kHertzPerKilohertz, 'g', digits) +
                   " kHz";
}

// Significant digits of a frequency label and of a readout.
constexpr int kLabelDigits = 3;
constexpr int kReadoutDigits = 4;

// Decimals of the gain and phase read out at the cursor.
constexpr int kGainDecimals = 2;
constexpr int kPhaseDecimals = 1;

}  // namespace

BodePlotWidget::BodePlotWidget(QWidget* parent)
    : PlotCanvas(kMinimumSize, parent),
      gain_{kGainsPerDivision, " dB"},
      phase_{kPhasesPerDivision, "°"} {
  UpdatePlotMargins();
  SetFrequencyRange(kDefaultLowFrequency, kDefaultHighFrequency);
}

void BodePlotWidget::SetFrequencyRange(double low, double high) {
  low_frequency_ = low;
  high_frequency_ = high;
  points_.clear();
  gain_.top = kDefaultGainTop;
  gain_.per_division = kDefaultGainPerDivision;
  phase_.top = kDefaultPhaseTop;
  phase_.per_division = kDefaultPhasePerDivision;
  update();
}

void BodePlotWidget::AddPoint(const BodePoint& point) {
  points_.push_back(point);
  FitScale(gain_, &BodePoint::gain);
  FitScale(phase_, &BodePoint::phase);
  update();
}

// Zooms scale in as far as its values per division allow while still showing
// the value of every point, with its top on a whole division.
void BodePlotWidget::FitScale(Scale& scale, double BodePoint::* value) {
  const auto [low, high] = std::ranges::minmax(points_, {}, value);
  for (const double per_division : scale.per_divisions) {
    const double top = std::ceil(high.*value / per_division) * per_division;
    if (top - kNumDivisions * per_division <= low.*value) {
      scale.top = top;
      scale.per_division = per_division;
      return;
    }
  }
  scale.per_division = scale.per_divisions.back();
  scale.top = std::ceil(high.*value / scale.per_division) * scale.per_division;
}

void BodePlotWidget::UpdatePlotMargins() {
  // Room for the widest labels on the left and the frequencies below, and the
  // height of a label between the panes.
  const QFontMetrics metrics = fontMetrics();
  int width = 0;
  for (const QString& label : {QString("-180°"), QString("-100 dB")}) {
    width = std::max(width, metrics.horizontalAdvance(label));
  }
  plot_margins_ = QMargins(width + 2 * kLabelGap, kLabelGap,
                           metrics.horizontalAdvance(FormatFrequency(
                               kDefaultHighFrequency, kLabelDigits)) /
                               2,
                           metrics.height() + 2 * kLabelGap);
}

// Returns the areas of the gain pane on top and the phase pane below, a label's
// height apart.
QRect BodePlotWidget::GetGainRect() const {
  const QRect plot = rect().marginsRemoved(plot_margins_);
  const int height = (plot.height() - fontMetrics().height()) / 2;
  return QRect(plot.left(), plot.top(), plot.width(), height);
}

QRect BodePlotWidget::GetPhaseRect() const {
  const QRect gain = GetGainRect();
  return gain.translated(0, gain.height() + fontMetrics().height());
}

// Returns the x of frequency in a pane width pixels wide, on the log axis.
double BodePlotWidget::GetX(double frequency, int width) const {
  return std::log(frequency / low_frequency_) /
         std::log(high_frequency_ / low_frequency_) * width;
}

// Returns the frequency at x in a pane width pixels wide.
double BodePlotWidget::GetFrequencyAt(double x, int width) const {
  return low_frequency_ * std::pow(high_frequency_ / low_frequency_, x / width);
}

void BodePlotWidget::DrawPlot(QPainter& painter) {
  const QRect phase = GetPhaseRect();
  DrawPane(painter, GetGainRect(), gain_, &BodePoint::gain, kGainColor);
  DrawPane(painter, phase, phase_, &BodePoint::phase, kPhaseColor);
  DrawFrequencyLabels(painter, phase);
}

// The cursor reads out over both panes.
QRect BodePlotWidget::GetCursorArea() const {
  return GetGainRect().united(GetPhaseRect());
}

// Draws pane: its grid, the labels of scale on its left, and the curve of
// value of the points in color.
void BodePlotWidget::DrawPane(QPainter& painter, const QRect& pane,
                              const Scale& scale, double BodePoint::* value,
                              const QColor& color) {
  const int w = pane.width();
  const int h = pane.height();

  // A line per step of each decade, brighter at the start of a decade.
  const int first_decade = int(std::floor(std::log10(low_frequency_)));
  const int last_decade = int(std::ceil(std::log10(high_frequency_)));
  for (int decade = first_decade; decade <= last_decade; ++decade) {
    for (int step = 1; step <= kStepsPerDecade; ++step) {
      const double frequency = step * std::pow(10.0, decade);
      if (frequency < low_frequency_ || frequency > high_frequency_) continue;
      painter.setPen(
          QPen(step == 1 ? kGridColor : kMinorGridColor, 1, Qt::DotLine));
      const double x = pane.left() + GetX(frequency, w);
      painter.drawLine(QPointF(x, pane.top()), QPointF(x, pane.bottom()));
    }
  }

  // A line and a label per division.
  const QRect label_bounds(0, pane.top(), pane.left(), h);
  for (int i = 0; i <= kNumDivisions; ++i) {
    const int y = pane.top() + h * i / kNumDivisions;
    painter.setPen(QPen(kGridColor, 1, Qt::DotLine));
    painter.drawLine(pane.left(), y, pane.right(), y);
    painter.setPen(kLabelColor);
    const double label = scale.top - i * scale.per_division;
    DrawLabel(painter, QString::number(label) + scale.unit,
              QPoint(pane.left() / 2, y), label_bounds);
  }
  painter.setPen(QPen(kAxisColor, 1, Qt::SolidLine));
  painter.drawRect(pane.adjusted(0, 0, -1, -1));

  // The curve, through a dot at each point.
  painter.save();
  painter.setClipRect(pane);
  const double bottom = scale.top - kNumDivisions * scale.per_division;
  QPolygonF curve;
  for (const BodePoint& point : points_) {
    curve.append(QPointF(
        pane.left() + GetX(point.frequency, w),
        pane.top() + (scale.top - point.*value) / (scale.top - bottom) * h));
  }
  painter.setPen(QPen(color, kTraceWidth));
  painter.drawPolyline(curve);
  painter.setBrush(color);
  for (const QPointF& point : curve) {
    painter.drawEllipse(point, kPointRadius, kPointRadius);
  }
  painter.restore();
}

// Draws a label below pane at the start of each decade.
void BodePlotWidget::DrawFrequencyLabels(QPainter& painter, const QRect& pane) {
  painter.setPen(kLabelColor);
  const int y = pane.bottom() + kLabelGap + painter.fontMetrics().height() / 2;
  const int first_decade = int(std::ceil(std::log10(low_frequency_)));
  const int last_decade = int(std::floor(std::log10(high_frequency_)));
  for (int decade = first_decade; decade <= last_decade; ++decade) {
    const double frequency = std::pow(10.0, decade);
    const int x = pane.left() + int(GetX(frequency, pane.width()));
    DrawLabel(painter, FormatFrequency(frequency, kLabelDigits), QPoint(x, y),
              rect());
  }
}

// Returns the frequency at x, in pixels from the left edge of a pane width
// pixels wide, and the gain and phase of the point nearest to it.
std::vector<ReadoutLine> BodePlotWidget::GetReadout(double x, int width) const {
  const double frequency = GetFrequencyAt(x, width);
  std::vector<ReadoutLine> lines = {
      {"f = " + FormatFrequency(frequency, kReadoutDigits), kLabelColor}};
  if (points_.empty()) return lines;
  // Nearest on the log axis, as the points are spread on it.
  const BodePoint& nearest = *std::ranges::min_element(
      points_, {}, [frequency](const BodePoint& point) {
        return std::abs(std::log(point.frequency / frequency));
      });
  lines.push_back({FormatFrequency(nearest.frequency, kReadoutDigits) + ": " +
                       QString::number(nearest.gain, 'f', kGainDecimals) +
                       " dB",
                   kGainColor});
  lines.push_back({FormatFrequency(nearest.frequency, kReadoutDigits) + ": " +
                       QString::number(nearest.phase, 'f', kPhaseDecimals) +
                       "°",
                   kPhaseColor});
  return lines;
}

}  // namespace m1k_scope::ui
