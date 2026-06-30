// Bitrate adaptation under the USB-2 ceiling (M5). AIMD-style: multiplicative decrease on a congestion
// signal (slow link / send backpressure), additive increase while healthy, clamped to [floor, cap].
// Pure + unit-tested. The session applies the result via FORMAT_CHANGE / encoder reconfigure.
#ifndef TD_CORE_BITRATE_CONTROLLER_HPP
#define TD_CORE_BITRATE_CONTROLLER_HPP

#include <algorithm>
#include <cstdint>

namespace td::core {

class BitrateController {
 public:
  BitrateController(std::uint32_t start_kbps, std::uint32_t floor_kbps, std::uint32_t cap_kbps)
      : floor_(floor_kbps), cap_(cap_kbps), current_(std::clamp(start_kbps, floor_kbps, cap_kbps)) {}

  std::uint32_t current() const { return current_; }

  // Congestion (a frame couldn't be sent in time / link is behind): drop fast.
  std::uint32_t OnCongestion() {
    current_ = std::max(floor_, static_cast<std::uint32_t>(current_ * 7 / 10));  // x0.7
    return current_;
  }

  // A healthy interval elapsed with no congestion: probe upward by ~5% of the cap.
  std::uint32_t OnHealthy() {
    const std::uint32_t step = std::max<std::uint32_t>(1, cap_ / 20);
    current_ = std::min(cap_, current_ + step);
    return current_;
  }

 private:
  std::uint32_t floor_;
  std::uint32_t cap_;
  std::uint32_t current_;
};

}  // namespace td::core

#endif  // TD_CORE_BITRATE_CONTROLLER_HPP
