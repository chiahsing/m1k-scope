#include "device.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <libsmu/libsmu.hpp>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace m1k_scope::core {

namespace {

// Timeout for a single device read, so the loop can notice a stop request.
constexpr int kReadTimeoutMs = 100;

// Samples requested per device read, i.e. 10 ms at m1k::kSampleRate. A read
// waits until this many samples have arrived, and frames are only cut after a
// read, so a frame may be sent up to 10 ms after its last sample arrived.
// Smaller reads send frames sooner but loop more often.
constexpr std::size_t kReadSamples = 1000;

// set_led() bit masks. libsmu's LED enum holds pin numbers, not these bits.
constexpr unsigned kLedRed = 0b001;
constexpr unsigned kLedGreen = 0b010;

// How long a generator setting takes to show in the samples read, from when
// it is set: samples already acquired but not read yet, and the output queued
// in libsmu and the M1K. Measured at 41 to 71 ms over 20 steps between 0 V
// and 5 V, so this leaves a margin.
constexpr std::chrono::milliseconds kGeneratorLatency(100);

// How long Auto mode waits for a trigger, from when one may be taken after the
// last frame, before sending the latest time window.
constexpr std::chrono::milliseconds kAutoTriggerTimeout(100);

// Number of samples covering duration at m1k::kSampleRate.
std::size_t GetSampleCount(std::chrono::microseconds duration) {
  return std::lround(m1k::kSampleRate *
                     std::chrono::duration<double>(duration).count());
}

// Number of samples covering kGeneratorLatency.
std::int64_t GetGeneratorLatencySamples() {
  return std::int64_t(GetSampleCount(kGeneratorLatency));
}

// Duration covered by samples at m1k::kSampleRate.
std::chrono::microseconds GetDuration(std::size_t samples) {
  return std::chrono::microseconds(std::chrono::seconds(samples)) /
         m1k::kSampleRate;
}

}  // namespace

Device::Device() : generator_applied_sample_(GetGeneratorLatencySamples()) {
  UpdateFrameTriggerSettings();
}

Device::~Device() { StopAcquisition(); }

bool Device::SetGeneratorConfig(int ch, const GeneratorConfig& config) {
  if (ch < 0 || ch >= m1k::kChannelCount) return false;
  generator_configs_[ch] = config;
  // While stopped, sample_count_ is 0: the settings apply when acquisition
  // starts, numbering its samples from 0.
  generator_applied_sample_ = sample_count_ + GetGeneratorLatencySamples();
  return !is_running_ || ApplyGeneratorConfig(ch);
}

void Device::SetTrigger(const TriggerConfig& config) {
  trigger_config_ = config;
  UpdateFrameTriggerSettings();
}

void Device::SetTimeWindow(std::chrono::microseconds time_window) {
  time_window_ = time_window;
  UpdateFrameTriggerSettings();
}

void Device::UpdateFrameTriggerSettings() {
  const FrameTrigger::Settings settings = FrameTrigger::Settings::Resolve(
      trigger_config_, GetSampleCount(time_window_));
  std::lock_guard lock(settings_mutex_);
  frame_trigger_settings_ = settings;
}

bool Device::Open() {
  if (!session_) {
    session_ = std::make_unique<smu::Session>();
  }
  if (session_->m_devices.empty() && session_->add_all() <= 0) return false;
  SetLed(kLedRed);
  return true;
}

bool Device::StartAcquisition() {
  if (is_running_) return true;
  if (!session_ || session_->m_devices.empty()) return false;
  if (time_window_ <= std::chrono::microseconds(0)) return false;

  auto start = [this] {
    if (session_->configure(m1k::kSampleRate) < 0) return false;
    // Drop samples left over from the last session, so reading starts with
    // the first sample of this one.
    (*session_->m_devices.begin())->flush(-1, /*read=*/true);
    for (int ch = 0; ch < m1k::kChannelCount; ++ch) {
      if (!ApplyGeneratorConfig(ch)) return false;
    }
    try {
      return session_->start(0) >= 0;
    } catch (const std::system_error&) {
      return false;
    }
  };
  if (!start()) {
    // Turn the M1K off as stopping does, so no channel is left sourcing a
    // generator setting already applied.
    session_->end();
    return false;
  }

  // Drop a frame left from the last acquisition, whose samples are numbered
  // apart from this one's.
  {
    std::lock_guard lock(frame_mutex_);
    has_new_frame_ = false;
  }
  is_running_ = true;
  worker_ = std::thread(&Device::RunAcquisition, this);
  SetLed(kLedGreen);
  return true;
}

void Device::StopAcquisition() {
  if (!is_running_) return;

  is_running_ = false;
  if (worker_.joinable()) worker_.join();
  session_->cancel();
  session_->end();
  SetLed(kLedRed);
  // The next acquisition numbers its samples from 0 again, and applies the
  // generator settings when it starts.
  sample_count_ = 0;
  generator_applied_sample_ = GetGeneratorLatencySamples();
}

bool Device::ApplyGeneratorConfig(int ch) {
  smu::Device* device = *session_->m_devices.begin();
  const GeneratorConfig& config = generator_configs_[ch];

  // A disabled channel floats and only measures. Stop its output buffer after
  // leaving the sourcing mode.
  if (!config.enabled) {
    device->set_mode(ch, HI_Z);
    device->flush(ch, false);
    return true;
  }

  // Queue the waveform before switching to a sourcing mode; libsmu busy-waits
  // for output samples in those modes.
  std::vector<float> buf = GenerateWaveform(config, m1k::kSampleRate);
  try {
    if (device->write(buf, ch, /*cyclic=*/true) < 0) return false;
  } catch (const std::exception&) {
    return false;
  }
  return device->set_mode(
             ch, config.mode == SourceMode::kCurrent ? SIMV : SVMI) >= 0;
}

void Device::SetLed(unsigned leds) {
  (*session_->m_devices.begin())->set_led(leds);
}

void Device::SetFrameReadyCallback(FrameReadyCallback callback) {
  frame_ready_callback_ = std::move(callback);
}

std::optional<Device::FrameInfo> Device::ProcessLatestFrame(Traces& traces) {
  {
    std::lock_guard lock(frame_mutex_);
    if (!has_new_frame_) return std::nullopt;
    std::swap(front_, ready_);
    has_new_frame_ = false;
  }

  // Extract the measured value of every measurement from the raw samples of
  // the front frame. Frames longer than kMaxSamplesPerMeasurement are split
  // into that many equal-as-possible runs, each averaged into one sample;
  // shorter frames are kept as is. The acquisition thread never touches the
  // front buffer, so no lock is needed here.
  const std::vector<m1k::Sample>& frame = frames_[front_].samples;
  const std::size_t size = frame.size();
  const std::size_t count =
      std::min<std::size_t>(size, kMaxSamplesPerMeasurement);
  // The index of a measurement is the index of its value in a sample.
  for (int m = 0; m < kMeasurementCount; ++m) {
    std::vector<float>& samples = traces[m];
    samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      // Run i covers raw samples [i * size / count, (i + 1) * size / count).
      const std::size_t begin = i * size / count;
      const std::size_t end = (i + 1) * size / count;
      float sum = 0;
      for (std::size_t j = begin; j < end; ++j) sum += frame[j][m];
      samples[i] = sum / (end - begin);
    }
  }
  return FrameInfo{.window = GetDuration(size),
                   .first_sample = frames_[front_].first_sample};
}

void Device::RunAcquisition() {
  smu::Device* device = *session_->m_devices.begin();
  FrameTrigger::Settings settings;
  {
    std::lock_guard lock(settings_mutex_);
    settings = frame_trigger_settings_;
  }
  FrameTrigger frame_trigger(settings, GetSampleCount(kAutoTriggerTimeout));
  std::vector<m1k::Sample> buf;
  buf.reserve(kReadSamples);

  while (is_running_) {
    // The read returns early after kReadTimeoutMs so a stop request is noticed
    // promptly.
    try {
      if (device->read(buf, kReadSamples, kReadTimeoutMs) < 0) {
        // Read error: drop the history and retry after a pause. Leaving the
        // loop would stop acquisition while is_running_ still says it runs.
        frame_trigger.Reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(kReadTimeoutMs));
        continue;
      }
    } catch (const std::system_error&) {
      // Sample overflow: samples were lost, so drop the history.
      frame_trigger.Reset();
      continue;
    }

    {
      std::lock_guard lock(settings_mutex_);
      if (frame_trigger_settings_ != settings) {
        settings = frame_trigger_settings_;
        frame_trigger.SetSettings(settings);
      }
    }

    // Once a frame is due, publish it as the ready buffer and notify the
    // consumer. A ready frame the consumer has not taken yet is simply
    // replaced.
    // Lost samples are not numbered, as their number is unknown.
    const std::int64_t first_sample = sample_count_;
    sample_count_ += std::int64_t(buf.size());
    if (!frame_trigger.Append(buf, first_sample, frames_[back_])) continue;
    {
      std::lock_guard lock(frame_mutex_);
      std::swap(back_, ready_);
      has_new_frame_ = true;
    }
    if (frame_ready_callback_) frame_ready_callback_();
  }
}

}  // namespace m1k_scope::core
