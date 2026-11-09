#include "generator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace m1k_scope::core {

namespace {

// Minimum length of a generated buffer, so short periods are repeated enough
// times to keep the frequency rounding error small.
constexpr double kMinSamples = 1000;

// Number of steps of the Stairstep waveform per period.
constexpr int kStairSteps = 10;

// Value of a periodic waveform in -1..1 at normalized phase p in [0, 1).
double GetShapeValue(const GeneratorConfig& config, double p) {
  switch (config.type) {
    case WaveformType::kConstant:
      return 0.0;
    case WaveformType::kSine:
      return std::sin(2 * std::numbers::pi * p);
    case WaveformType::kSquare:
      return p < config.duty_cycle ? 1.0 : -1.0;
    case WaveformType::kTriangle:
      return p < 0.5 ? 4 * p - 1 : 3 - 4 * p;
    case WaveformType::kSawtooth:
      return 2 * p - 1;
    case WaveformType::kStairstep:
      return std::floor(p * kStairSteps) / (kStairSteps - 1) * 2 - 1;
  }
  return 0.0;
}

}  // namespace

std::vector<float> GenerateWaveform(const GeneratorConfig& config,
                                    double sample_rate) {
  if (config.type == WaveformType::kConstant) {
    return std::vector<float>(kMinSamples, config.constant);
  }

  // Fit a whole number of periods into at least kMinSamples samples.
  const double samples_per_period = sample_rate / config.frequency;
  const double periods = std::ceil(kMinSamples / samples_per_period);
  const auto length =
      std::max<std::size_t>(1, std::lround(periods * samples_per_period));

  std::vector<float> buf(length);
  for (std::size_t i = 0; i < length; ++i) {
    const double p = std::fmod(i * periods / length + config.phase / 360, 1.0);
    buf[i] = (config.min + config.max) / 2 +
             (config.max - config.min) / 2 * GetShapeValue(config, p);
  }
  return buf;
}

}  // namespace m1k_scope::core
