#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "measurement.h"

namespace m1k_scope::core {

class Device;

// A point of a frequency response.
struct BodePoint {
  double frequency;  // In Hertz
  double gain;       // In dB
  double phase;      // In degrees
};

// Measures a frequency response point by point with the M1K.
//
// Ch A drives the input of the circuit with a sine wave between two levels and
// measures it; Ch B floats (Hi-Z) and measures the output. A sweep steps the
// sine wave through frequencies spread evenly on a log scale. At each
// frequency, the circuit is left to settle for a while from the first sample
// with the new sine wave, then the frames after it, a whole number of periods
// long each, are measured and averaged:
// the gain is the ratio of the output's amplitude to the input's, and the
// phase the difference of their phases, less the phase from the M1K sampling
// Ch B slightly before Ch A.
class BodeSweep {
 public:
  // Channels driving and measuring the circuit.
  static constexpr int kInputChannel = 0;
  static constexpr int kOutputChannel = 1;

  // What a sweep sweeps.
  struct Settings {
    double from;            // Lowest frequency, in Hertz
    double to;              // Highest frequency, in Hertz
    int points_per_decade;  // Frequencies per decade
    double min;             // Lowest level of the sine wave, in Volts
    double max;             // Highest level of the sine wave, in Volts
  };

  // Constructs a sweep driving device, which must outlive it.
  explicit BodeSweep(Device& device);

  // Sets up the M1K for settings and starts acquisition. Returns false, with
  // the sweep stopped, if acquisition fails to start.
  bool Start(const Settings& settings);

  // Stops the sweep and acquisition.
  void Stop();

  // Returns whether a sweep runs.
  bool IsRunning() const { return running_; }

  // Takes the latest frame, if a new one is ready, and returns the point it
  // completes, if any. The sweep stops after its last point.
  std::optional<BodePoint> TakeFrame();

 private:
  void StartPoint();

  Device& device_;
  Settings settings_{};

  // The frequencies of the sweep, in Hertz, the index of the one measured,
  // and the number of the first sample, as in Device::FrameInfo, of a frame
  // to measure there, once the circuit has settled.
  std::vector<double> frequencies_;
  std::size_t index_{0};
  std::int64_t settled_sample_{0};

  // Traces of the latest frame, refilled in place.
  Traces traces_;

  // Whether a sweep runs.
  bool running_{false};

  // Frames measured at the frequency measured, of those to average, and the
  // sum of their responses: output over input.
  int frames_to_average_{0};
  int frames_averaged_{0};
  std::complex<double> response_sum_;
};

}  // namespace m1k_scope::core
