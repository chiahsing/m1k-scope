#include "frame_trigger.h"

#include <algorithm>

namespace m1k_scope::core {

namespace {

// How far below the level the trigger measurement must go to arm the edge, as
// a fraction of the range of its quantity: 25 mV or 2 mA. It keeps noise
// around the level from firing the edge again and again.
constexpr double kHysteresisFraction = 0.005;

// Width of the range the M1K measures quantity over, in its unit.
constexpr double GetMeasuredRange(Quantity quantity) {
  return GetMaxValue(quantity) - GetMinValue(quantity);
}

}  // namespace

FrameTrigger::Settings FrameTrigger::Settings::Resolve(
    const TriggerConfig& config, std::size_t window_samples) {
  if (config.measurement == kNoTrigger) {
    return {.window_samples = window_samples};
  }
  // The index of a measurement is the index of its value in a sample.
  const Quantity quantity = kMeasurements[config.measurement].quantity;
  return {
      .window_samples = window_samples,
      .enabled = true,
      .column = config.measurement,
      .edge = config.edge,
      .level = config.level,
      .hysteresis =
          static_cast<float>(kHysteresisFraction * GetMeasuredRange(quantity)),
      .mode = config.mode,
  };
}

FrameTrigger::FrameTrigger(const Settings& settings,
                           std::size_t auto_timeout_samples)
    : auto_timeout_samples_(auto_timeout_samples), settings_(settings) {}

void FrameTrigger::SetSettings(const Settings& settings) {
  settings_ = settings;
  Reset();
}

void FrameTrigger::Reset() {
  history_.clear();
  scanned_ = 0;
  armed_ = false;
  trigger_point_.reset();
  sent_end_ = 0;
}

bool FrameTrigger::Append(const std::vector<m1k::Sample>& samples,
                          std::int64_t first_sample, Frame& frame) {
  const std::size_t window = settings_.window_samples;
  history_.insert(history_.end(), samples.begin(), samples.end());
  // Number of the sample after the latest one, which ends the history.
  const std::int64_t end_sample = first_sample + std::int64_t(samples.size());

  // Only one trigger point waits at a time; search on once it has been sent.
  if (settings_.enabled && !trigger_point_) trigger_point_ = SearchTrigger();

  // Decide whether a frame is due, and where its time window starts in the
  // history.
  const std::size_t size = history_.size();
  std::optional<std::size_t> start;
  if (trigger_point_) {
    // Due once the whole time window around the trigger point has arrived.
    const std::size_t window_start = *trigger_point_ - window / 2;
    if (size >= window_start + window) {
      start = window_start;
      trigger_point_.reset();
    }
  } else if (size >= window) {
    // Without a waiting trigger point, the latest time window is due one time
    // window after the last frame with the trigger off. In Auto mode it is due
    // once no trigger point has been found for the auto timeout after the
    // earliest one SearchTrigger() takes, half a time window after the last
    // frame; timing out sooner would preempt every trigger point once half a
    // time window outlasts the timeout. Normal mode waits for a trigger point.
    std::optional<std::size_t> interval;
    if (!settings_.enabled) {
      interval = window;
    } else if (settings_.mode == TriggerMode::kAuto) {
      interval = window / 2 + auto_timeout_samples_;
    }
    if (interval && std::int64_t(size) >= sent_end_ + std::int64_t(*interval)) {
      start = size - window;
    }
  }
  // Copy the frame out before trimming may erase its samples.
  if (start) {
    frame.samples.assign(history_.begin() + *start,
                         history_.begin() + *start + window);
    frame.first_sample = end_sample - std::int64_t(size - *start);
    sent_end_ = *start + window;
  }

  // Trim the history to its latest time window once it holds two, which moves
  // one time window per time window of new samples. Shift the indices to
  // match. scanned_ only points into the erased part while the trigger is off,
  // when it does not matter, so it becomes 0. sent_end_ goes negative instead,
  // as the time since the last frame matters to Auto mode and the trigger off.
  // A waiting trigger point is always kept.
  if (history_.size() >= 2 * window) {
    const std::size_t drop = history_.size() - window;
    history_.erase(history_.begin(), history_.begin() + drop);
    scanned_ -= std::min(scanned_, drop);
    sent_end_ -= std::int64_t(drop);
    if (trigger_point_) *trigger_point_ -= drop;
  }
  return start.has_value();
}

std::optional<std::size_t> FrameTrigger::SearchTrigger() {
  // A trigger point's time window starts half a time window before it, so take
  // none less than that after the last frame, nor before the history does.
  const std::int64_t half_window = settings_.window_samples / 2;
  const auto first =
      std::size_t(std::max(sent_end_ + half_window, half_window));

  // Flip the values of a falling edge so it can be searched as a rising one.
  const float sign = settings_.edge == TriggerEdge::kFalling ? -1 : 1;
  const float level = sign * settings_.level;

  // Arm and fire as the class comment says. A crossing before first still
  // disarms the edge, so a trigger point always needs a crossing from below of
  // its own. scanned_ moves past the trigger point, so the next search resumes
  // after it.
  while (scanned_ < history_.size()) {
    const std::size_t i = scanned_++;
    const float value = sign * history_[i][settings_.column];
    if (value < level - settings_.hysteresis) {
      armed_ = true;
    } else if (armed_ && value >= level) {
      armed_ = false;
      if (i >= first) return i;
    }
  }
  return std::nullopt;
}

}  // namespace m1k_scope::core
