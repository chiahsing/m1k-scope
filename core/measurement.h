#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Hardware facts about the ADALM1000 (M1K) and the samples libsmu returns.
namespace m1k_scope::core::m1k {

// Number of hardware channels.
constexpr int kChannelCount = 2;

// Values a sample holds per channel: voltage, then current.
constexpr int kValuesPerChannel = 2;

// One sample exactly as libsmu returns it:
// {A voltage, A current, B voltage, B current}.
using Sample = std::array<float, kChannelCount * kValuesPerChannel>;

// Voltage range the M1K sources and measures, in Volts.
constexpr double kMinVoltage = 0.0;
constexpr double kMaxVoltage = 5.0;

// Largest current the M1K sources or measures in either direction, in Amperes.
constexpr double kMaxCurrent = 0.2;

// Sample rate the M1K runs at. This is its native rate; at lower rates the
// output glitches at every USB packet boundary.
constexpr int kSampleRate = 100000;

}  // namespace m1k_scope::core::m1k

namespace m1k_scope::core {

// Physical quantity measured on a channel.
enum class Quantity {
  kVoltage,  // In Volts
  kCurrent,  // In Amperes
};

// One of the values an M1K measures: a quantity on a channel.
struct Measurement {
  int channel;
  Quantity quantity;
};

// Number of measurements, one per value of an m1k::Sample.
constexpr int kMeasurementCount = m1k::kChannelCount * m1k::kValuesPerChannel;

// All measurements, in the order of the values of an m1k::Sample, so the index
// of a measurement is the index of its value in a sample.
constexpr std::array<Measurement, kMeasurementCount> kMeasurements = {{
    {0, Quantity::kVoltage},
    {0, Quantity::kCurrent},
    {1, Quantity::kVoltage},
    {1, Quantity::kCurrent},
}};

// Index into kMeasurements of quantity on channel ch.
constexpr int GetMeasurementIndex(int ch, Quantity quantity) {
  for (int m = 0; m < kMeasurementCount; ++m) {
    if (kMeasurements[m].channel == ch &&
        kMeasurements[m].quantity == quantity) {
      return m;
    }
  }
  return -1;
}

// Lowest and highest value the M1K measures of quantity, in its unit.
constexpr double GetMinValue(Quantity quantity) {
  return quantity == Quantity::kCurrent ? -m1k::kMaxCurrent : m1k::kMinVoltage;
}
constexpr double GetMaxValue(Quantity quantity) {
  return quantity == Quantity::kCurrent ? m1k::kMaxCurrent : m1k::kMaxVoltage;
}

// Samples of every measurement over one frame, indexed like kMeasurements.
using Traces = std::array<std::vector<float>, kMeasurementCount>;

}  // namespace m1k_scope::core
