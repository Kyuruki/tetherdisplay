// Host configuration (M6): the small set of user-tunable settings, persisted locally as plain
// key=value text. NO secrets here — pairing keys live in OS secure storage. Pure + unit-tested; values
// are clamped to safe ranges on load so a hand-edited file can't push the encoder past the USB-2 budget.
#ifndef TD_CORE_CONFIG_HPP
#define TD_CORE_CONFIG_HPP

#include "td/encode/encoder.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace td::core {

struct Config {
  std::uint16_t device_port = 2345;          // iPad app's listening port
  bool          use_hevc = true;             // false => H.264
  std::uint32_t target_bitrate_kbps = 50000;
  std::uint32_t max_bitrate_kbps = encode::kUsb2VideoCeilingKbps;  // 80000
  std::uint32_t fps = 60;
  bool operator==(const Config&) const = default;
};

// Clamp every field to a safe range (port >= 1; bitrates within [5000, USB-2 ceiling] with target<=max;
// fps in [1, 240]). Returns the same object for chaining.
Config& Clamp(Config& c);

// Parse from key=value text. Unknown keys are ignored; missing keys keep their default; the result is
// clamped. Keys: device_port, codec(hevc|h264), target_bitrate_kbps, max_bitrate_kbps, fps.
Config ParseConfig(std::string_view text);
std::string SerializeConfig(const Config& c);

bool SaveConfig(const Config& c, const std::string& path);
std::optional<Config> LoadConfig(const std::string& path);  // nullopt if the file is missing

}  // namespace td::core

#endif  // TD_CORE_CONFIG_HPP
