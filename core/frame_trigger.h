#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "measurement.h"

namespace m1k_scope::core {

// Measurement index meaning the trigger is off.
constexpr int kNoTrigger = -1;

// Direction in which the trigger measurement must cross the level.
enum class TriggerEdge {
  kRising,
  kFalling,
};

// What the display does while the trigger condition is not met.
enum class TriggerMode {
  kAuto,    // Free-run after a timeout, so the signal is still visible
  kNormal,  // Keep showing the last triggered frame
};

// Trigger settings as the user picks them, in terms of measurements and their
// units. At most one measurement triggers at a time. Device resolves them into
// FrameTrigger::Settings for the acquisition thread.
struct TriggerConfig {
  int measurement = kNoTrigger;  // Index into kMeasurements, if any
  TriggerEdge edge = TriggerEdge::kRising;
  float level = 0;  // In the measurement quantity's unit
  TriggerMode mode = TriggerMode::kAuto;

  bool operator==(const TriggerConfig& other) const = default;
};

// Raw samples of one time window, as returned by libsmu at m1k::kSampleRate,
// and the number of the first one, as the caller of FrameTrigger::Append()
// numbers them.
struct Frame {
  std::vector<m1k::Sample> samples;
  std::int64_t first_sample = 0;
};

// Picks the frames to display from the raw samples read from the M1K, based on
// the trigger settings. A frame is one time window of raw samples.
//
// The acquisition thread appends the samples it reads; FrameTrigger keeps them
// as its history and searches it for trigger points. Once a trigger point is
// found, or Auto mode times out, and the frame is complete, Append() returns
// the frame: the time window centered on the trigger point, or on a timeout the
// latest time window. With the trigger off, Append() returns the latest time
// window every time window.
//
// The caller numbers the samples it appends, and each frame carries the number
// of its first sample, so it tells when the frame was acquired relative to any
// other sample.
//
// History: a vector of the latest raw samples, oldest first, appended to at
// the end. Positions in it are plain indices: how far the trigger search has
// got, a trigger point waiting for the rest of its time window, and the end of
// the last frame. Once the history holds two time windows, all but the latest
// one are erased from the front and the indices are shifted to match; the end
// of the last frame may go negative, so the time since it stays known. A
// trigger point waiting for its later half is always within the latest half
// window, so its whole time window survives the trimming.
//
// Trigger search: the samples of the trigger measurement are scanned once each,
// in order, resuming where the last search stopped. The edge is armed when the
// value goes below the level minus the hysteresis, and fires when it then
// reaches the level, which also disarms it; a falling edge is searched as a
// rising one by negating the values and the level. The hysteresis keeps noise
// around the level from firing again and again. A trigger point is only taken
// at least half a time window after the end of the last frame, so its time
// window has samples before it and never overlaps the last frame.
class FrameTrigger {
 public:
  // Trigger settings in terms of raw samples: TriggerConfig resolved to a
  // sample value and hysteresis, plus the time window as a number of samples.
  struct Settings {
    // Number of raw samples in a time window, and so in every frame.
    std::size_t window_samples = 0;

    // The trigger.
    bool enabled = false;  // False if the trigger is off
    int column = 0;        // Index into a sample of the trigger measurement
    TriggerEdge edge = TriggerEdge::kRising;
    float level = 0;
    float hysteresis = 0;  // How far below the level the value must go to arm
    TriggerMode mode = TriggerMode::kAuto;

    // Resolves config for time windows of window_samples samples.
    static Settings Resolve(const TriggerConfig& config,
                            std::size_t window_samples);

    bool operator==(const Settings& other) const = default;
  };

  // auto_timeout_samples is the number of samples Auto mode waits for a
  // trigger point.
  FrameTrigger(const Settings& settings, std::size_t auto_timeout_samples);

  // Replaces the settings and restarts as Reset() does.
  void SetSettings(const Settings& settings);

  // Forgets the history, e.g. after samples were lost.
  void Reset();

  // Appends samples, the first of them numbered first_sample and the others
  // following it, to the history. If a frame is due, fills frame with it and
  // returns true.
  bool Append(const std::vector<m1k::Sample>& samples,
              std::int64_t first_sample, Frame& frame);

 private:
  std::optional<std::size_t> SearchTrigger();

  // Number of raw samples Auto mode waits for a trigger point, from the
  // earliest one that may be taken, before sending the latest time window.
  const std::size_t auto_timeout_samples_;

  Settings settings_;

  // Latest raw samples, oldest first. It is trimmed to one time window once it
  // reaches two, so it always holds the whole time window around a trigger
  // point still waiting for its later half. Trimming keeps its capacity, so it
  // stops allocating after the first few appends.
  std::vector<m1k::Sample> history_;

  // State of the history, as indices into it.
  std::size_t scanned_{0};  // Samples searched for triggers
  bool armed_{false};       // Whether the value has crossed the hysteresis band
  std::optional<std::size_t> trigger_point_;  // Waiting for its later half
  std::int64_t sent_end_{0};  // End of the last frame; negative if trimmed
};

}  // namespace m1k_scope::core
