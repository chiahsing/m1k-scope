#include "bode_sweep.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "device.h"

namespace m1k_scope::core {

namespace {

// The measurement at each frequency averages frames for at least this long,
// to average out noise. A frame covers whole periods, at least kMinPeriods,
// and as many as fit in kMinMeasureTime. Frames longer than
// kMaxSamplesPerMeasurement samples come averaged down to that many, so they
// cover at most kMaxPeriods, keeping kMinPointsPerPeriod per period.
constexpr std::chrono::milliseconds kMinMeasureTime(50);
constexpr int kMinPeriods = 5;
constexpr int kMinPointsPerPeriod = 20;
constexpr int kMaxPeriods = kMaxSamplesPerMeasurement / kMinPointsPerPeriod;

// How long the circuit is left to settle at each frequency, from the first
// sample with its sine wave: at least kSettlePeriods periods, and at least
// kMinSettleTime, as its response to the step between frequencies decays
// with time constants of its own.
constexpr int kSettlePeriods = 5;
constexpr std::chrono::milliseconds kMinSettleTime(50);

// Measurements of the two voltages compared.
constexpr int kInputMeasurement =
    GetMeasurementIndex(BodeSweep::kInputChannel, Quantity::kVoltage);
constexpr int kOutputMeasurement =
    GetMeasurementIndex(BodeSweep::kOutputChannel, Quantity::kVoltage);

// How much earlier the M1K samples Ch B's voltage than Ch A's, as its ADC
// takes the values of a sample in turn. Measured with Ch A wired straight to
// Ch B: the phase between them grows with frequency as this delay.
constexpr std::chrono::duration<double, std::micro> kOutputSkew(4.45);

// Returns the frequencies of a sweep from from to to, in Hertz, spread evenly
// on a log scale at points_per_decade, both ends included.
std::vector<double> GetFrequencies(double from, double to,
                                   int points_per_decade) {
  const int steps =
      std::max(1, int(std::lround(std::log10(to / from) * points_per_decade)));
  std::vector<double> frequencies(steps + 1);
  for (int i = 0; i <= steps; ++i) {
    frequencies[i] = from * std::pow(to / from, double(i) / steps);
  }
  return frequencies;
}

// Returns the time window of a frame at frequency, in Hertz: a whole number of
// samples covering a whole number of periods, those in kMinMeasureTime within
// kMinPeriods to kMaxPeriods.
std::chrono::microseconds GetFrameWindow(double frequency) {
  const double min_time =
      std::chrono::duration<double>(kMinMeasureTime).count();
  const double periods = std::clamp<double>(std::ceil(min_time * frequency),
                                            kMinPeriods, kMaxPeriods);
  const long samples = std::lround(periods * m1k::kSampleRate / frequency);
  return std::chrono::microseconds(samples * std::micro::den /
                                   m1k::kSampleRate);
}

// Returns how many samples the circuit is left to settle at frequency, in
// Hertz.
std::int64_t GetSettleSamples(double frequency) {
  const double seconds =
      std::max(kSettlePeriods / frequency,
               std::chrono::duration<double>(kMinSettleTime).count());
  return std::llround(seconds * m1k::kSampleRate);
}

// Returns the component at frequency, in Hertz, of samples evenly spread over
// window, as a phasor: its magnitude is the peak amplitude, its argument the
// phase in radians. The mean of the samples is removed first, so a DC level
// does not leak into it.
std::complex<double> MeasureTone(const std::vector<float>& samples,
                                 double frequency,
                                 std::chrono::duration<double> window) {
  if (samples.empty()) return {};
  double mean = 0;
  for (const float sample : samples) mean += sample;
  mean /= samples.size();

  // Correlate with a complex exponential at frequency: a single bin of a
  // discrete Fourier transform.
  const double step =
      2 * std::numbers::pi * frequency * window.count() / samples.size();
  std::complex<double> sum;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    sum += (samples[i] - mean) * std::polar(1.0, -step * i);
  }
  return 2.0 * sum / double(samples.size());
}

}  // namespace

BodeSweep::BodeSweep(Device& device) : device_(device) {}

bool BodeSweep::Start(const Settings& settings) {
  settings_ = settings;
  frequencies_ =
      GetFrequencies(settings.from, settings.to, settings.points_per_decade);
  index_ = 0;
  // Ch B floats and only measures.
  device_.SetGeneratorConfig(kOutputChannel, {});
  device_.SetTrigger({});
  running_ = true;
  StartPoint();
  // The circuit keeps running between frequencies, so its DC level settles
  // only once.
  if (!device_.StartAcquisition()) {
    Stop();
    return false;
  }
  return true;
}

void BodeSweep::Stop() {
  device_.StopAcquisition();
  running_ = false;
}

// Switches Ch A to the frequency measured, and frames to whole periods of it.
// While acquiring, both apply right away.
void BodeSweep::StartPoint() {
  const double frequency = frequencies_[index_];
  device_.SetGeneratorConfig(kInputChannel, {.enabled = true,
                                             .mode = SourceMode::kVoltage,
                                             .type = WaveformType::kSine,
                                             .min = settings_.min,
                                             .max = settings_.max,
                                             .frequency = frequency});
  settled_sample_ =
      device_.GetGeneratorAppliedSample() + GetSettleSamples(frequency);
  const std::chrono::microseconds frame_window = GetFrameWindow(frequency);
  device_.SetTimeWindow(frame_window);
  frames_to_average_ = std::max<int>(
      1,
      std::ceil(std::chrono::duration<double>(kMinMeasureTime) / frame_window));
  frames_averaged_ = 0;
  response_sum_ = 0;
}

std::optional<BodePoint> BodeSweep::TakeFrame() {
  if (!running_) return std::nullopt;
  // Measure only frames starting once the circuit has settled. Settling
  // outlasts the frames of the previous time window, so these all cover whole
  // periods.
  const std::optional<Device::FrameInfo> frame =
      device_.ProcessLatestFrame(traces_);
  if (!frame || frame->first_sample < settled_sample_) return std::nullopt;

  const double frequency = frequencies_[index_];
  const std::complex<double> input =
      MeasureTone(traces_[kInputMeasurement], frequency, frame->window);
  const std::complex<double> output =
      MeasureTone(traces_[kOutputMeasurement], frequency, frame->window);
  // Frames need not start at the same phase, so average their responses.
  response_sum_ += output / input;
  if (++frames_averaged_ < frames_to_average_) return std::nullopt;

  // Undo the phase the skew of the samples adds.
  const double skew = std::chrono::duration<double>(kOutputSkew).count();
  const std::complex<double> response =
      response_sum_ / double(frames_averaged_) *
      std::polar(1.0, -2 * std::numbers::pi * frequency * skew);
  const BodePoint point = {
      .frequency = frequency,
      .gain = 20 * std::log10(std::abs(response)),
      .phase = std::arg(response) * 180 / std::numbers::pi};

  if (++index_ == frequencies_.size()) {
    Stop();
  } else {
    StartPoint();
  }
  return point;
}

}  // namespace m1k_scope::core
