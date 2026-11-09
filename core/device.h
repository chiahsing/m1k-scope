#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "frame_trigger.h"
#include "generator.h"
#include "measurement.h"

namespace smu {
class Session;
}  // namespace smu

namespace m1k_scope::core {

// Maximum number of samples per measurement in Traces, covering one time
// window. Frames with more raw samples are averaged down; shorter ones are kept
// as is.
constexpr int kMaxSamplesPerMeasurement = 1000;

// Core oscilloscope controller managing device configuration, continuous
// sampling acquisition, triggering, and sample data streaming.
//
// Terms: the time window is the time span shown at once. A frame holds the raw
// samples of one time window to show.
//
// The acquisition thread passes the samples it reads to a FrameTrigger, which
// cuts frames from them as the trigger settings say.
//
// Frames are handed to the consumer thread through three frame buffers: the
// acquisition thread copies the frame into the back buffer, swaps it with the
// ready buffer and invokes the FrameReadyCallback. The consumer thread then
// calls ProcessLatestFrame(), which swaps the ready buffer into the front
// buffer and computes the Traces from it. Neither thread ever waits
// for the other beyond a buffer index swap or a trigger settings copy.
//
// Except where noted, methods must be called from the consumer thread.
class Device {
 public:
  // What ProcessLatestFrame() says of the frame it takes.
  struct FrameInfo {
    // Time window of the frame.
    std::chrono::microseconds window;
    // Number of its first sample, counted from the start of acquisition at
    // m1k::kSampleRate.
    std::int64_t first_sample;
  };

  // Invoked on the acquisition thread whenever a new frame is ready. It should
  // only schedule ProcessLatestFrame() on the consumer thread.
  using FrameReadyCallback = std::function<void()>;

  Device();
  ~Device();

  // Stores the function generator settings of channel ch and, while running,
  // applies them to the M1K right away. Returns false if applying failed.
  bool SetGeneratorConfig(int ch, const GeneratorConfig& config);

  // Returns the number of the first sample, as in FrameInfo, sure to be
  // acquired with the generator settings last set. Settings set while stopped
  // apply when acquisition starts, so this is then the number they will apply
  // at.
  std::int64_t GetGeneratorAppliedSample() const {
    return generator_applied_sample_;
  }

  // Returns the function generator settings of channel ch.
  const GeneratorConfig& GetGeneratorConfig(int ch) const {
    return generator_configs_[ch];
  }

  // Replaces the trigger settings. The acquisition thread picks them up with
  // its next read.
  void SetTrigger(const TriggerConfig& config);

  // Returns the trigger settings.
  const TriggerConfig& Trigger() const { return trigger_config_; }

  // Opens the first M1K device found and turns its LED red. Returns false if
  // no device is available. Must succeed before StartAcquisition() is called.
  bool Open();

  // Starts continuous sample acquisition from the opened device and turns its
  // LED green. A frame of the last acquisition not taken yet is dropped.
  // Returns false if the device fails to start, or if no time window has been
  // set with SetTimeWindow().
  bool StartAcquisition();

  // Halts sample acquisition, waits for the acquisition thread to exit, and
  // turns the LED red.
  void StopAcquisition();

  // Returns true if the acquisition worker is currently active.
  bool IsRunning() const { return is_running_; }

  // Returns the time window, i.e. the time span shown at once and covered by
  // one frame, or 0 until one is set.
  std::chrono::microseconds TimeWindow() const { return time_window_; }

  // Replaces the time window, a whole number of samples at
  // m1k::kSampleRate. The acquisition thread picks it up with its next
  // read; frames already taken keep their time window.
  void SetTimeWindow(std::chrono::microseconds time_window);

  // Registers the frame-ready notification. Must not be called while
  // acquisition is running.
  void SetFrameReadyCallback(FrameReadyCallback callback);

  // Takes the latest frame, if a new one is ready, and fills traces from it:
  // every measurement, each with up to kMaxSamplesPerMeasurement samples
  // averaged from the frame. Reuses the buffers already in traces, so passing
  // the same traces every time avoids allocations. Returns what the frame is,
  // its time window differing from TimeWindow() until a frame with a new time
  // window arrives, or nullopt, leaving traces as they were, if no new frame
  // is ready.
  std::optional<FrameInfo> ProcessLatestFrame(Traces& traces);

 private:
  void RunAcquisition();
  void UpdateFrameTriggerSettings();
  bool ApplyGeneratorConfig(int ch);
  void SetLed(unsigned leds);

  // Generator settings indexed by channel.
  std::array<GeneratorConfig, m1k::kChannelCount> generator_configs_;
  std::chrono::microseconds time_window_{0};
  FrameReadyCallback frame_ready_callback_{nullptr};

  // See GetGeneratorAppliedSample().
  std::int64_t generator_applied_sample_;

  // Samples the acquisition thread has read since acquisition started, which
  // is the number the next one gets; 0 while stopped, as the next acquisition
  // numbers its samples from 0. Written by the acquisition thread while it
  // runs.
  std::atomic<std::int64_t> sample_count_{0};

  TriggerConfig trigger_config_;

  // time_window_ and trigger_config_ resolved for the acquisition thread,
  // which copies them after every read. Protected by settings_mutex_.
  FrameTrigger::Settings frame_trigger_settings_;
  std::mutex settings_mutex_;

  // Three frame buffers. The indices below say which buffer plays which role;
  // the roles move between buffers by swapping indices, never by copying.
  std::array<Frame, 3> frames_;

  // Buffer the acquisition thread copies the next frame into. Only the
  // acquisition thread uses it.
  int back_{0};

  // Latest frame, waiting for the consumer. Once the back buffer holds a new
  // frame, the acquisition thread swaps it with this one. If the consumer has
  // not taken the previous frame yet, that frame is dropped.
  int ready_{1};  // Protected by frame_mutex_.

  // Frame the consumer thread is processing. ProcessLatestFrame() swaps it
  // with the ready buffer when a new frame is available. Only the consumer
  // thread uses it, so the acquisition thread never touches it.
  int front_{2};

  // True if the ready buffer holds a frame the consumer has not taken yet.
  bool has_new_frame_{false};  // Protected by frame_mutex_.

  std::mutex frame_mutex_;

  std::unique_ptr<smu::Session> session_;
  std::thread worker_;
  std::atomic<bool> is_running_{false};
};

}  // namespace m1k_scope::core
