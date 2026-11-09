#pragma once

#include <vector>

#include "measurement.h"

namespace m1k_scope::core {

// What a generating channel sources; the M1K measures the other quantity.
enum class SourceMode {
  kVoltage,  // Source voltage, measure current (SVMI), values in Volts
  kCurrent,  // Source current, measure voltage (SIMV), values in Amperes
};

// Mode in which a channel sources quantity.
constexpr SourceMode GetSourceMode(Quantity quantity) {
  return quantity == Quantity::kCurrent ? SourceMode::kCurrent
                                        : SourceMode::kVoltage;
}

// Supported waveform function types for function generation.
enum class WaveformType {
  kConstant,   // Static DC level
  kSine,       // Sinusoidal wave
  kSquare,     // Square wave with adjustable duty cycle
  kTriangle,   // Rises for half a period, then falls
  kSawtooth,   // Rises over the period, then drops
  kStairstep,  // Rises in equal steps over the period
};

// Function generator settings of one channel. Levels are in Volts or Amperes
// depending on mode.
struct GeneratorConfig {
  bool enabled = false;                         // Whether the channel generates
  SourceMode mode = SourceMode::kVoltage;       // Quantity being sourced
  WaveformType type = WaveformType::kConstant;  // Waveform function
  double constant = 0.0;     // Level of the Constant waveform
  double min = 0.0;          // Lowest level of periodic waveforms
  double max = 5.0;          // Highest level of periodic waveforms
  double frequency = 100.0;  // Frequency of periodic waveforms in Hertz
  double phase = 0.0;        // Phase of periodic waveforms in degrees
  double duty_cycle = 0.5;   // Fraction of the period a square wave is high

  bool operator==(const GeneratorConfig& other) const = default;
};

// Returns samples of the waveform described by config at sample_rate samples
// per second, for playing back cyclically. The buffer holds a whole number of
// periods, so repeating it is seamless; the frequency is rounded slightly to
// make that possible.
std::vector<float> GenerateWaveform(const GeneratorConfig& config,
                                    double sample_rate);

}  // namespace m1k_scope::core
