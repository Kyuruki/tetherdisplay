// Host config parse/serialize/clamp — portable. See config.hpp.
#include "td/core/config.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace td::core {
namespace {

constexpr std::uint32_t kMinBitrate = 5000;

std::string trim(std::string_view s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(b, e - b + 1));
}

std::optional<std::uint64_t> toU64(const std::string& s) {
  std::uint64_t v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
  return v;
}

}  // namespace

Config& Clamp(Config& c) {
  if (c.device_port == 0) c.device_port = 2345;
  c.fps = std::clamp<std::uint32_t>(c.fps, 1, 240);
  c.max_bitrate_kbps = std::clamp(c.max_bitrate_kbps, kMinBitrate, encode::kUsb2VideoCeilingKbps);
  c.target_bitrate_kbps = std::clamp(c.target_bitrate_kbps, kMinBitrate, c.max_bitrate_kbps);
  return c;
}

Config ParseConfig(std::string_view text) {
  Config c;
  std::istringstream in{std::string(text)};
  std::string line;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);  // strip comments
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string val = trim(line.substr(eq + 1));
    if (key == "device_port") {
      if (auto v = toU64(val); v && *v <= 65535) c.device_port = static_cast<std::uint16_t>(*v);
    } else if (key == "codec") {
      c.use_hevc = (val != "h264");
    } else if (key == "target_bitrate_kbps") {
      if (auto v = toU64(val)) c.target_bitrate_kbps = static_cast<std::uint32_t>(*v);
    } else if (key == "max_bitrate_kbps") {
      if (auto v = toU64(val)) c.max_bitrate_kbps = static_cast<std::uint32_t>(*v);
    } else if (key == "fps") {
      if (auto v = toU64(val)) c.fps = static_cast<std::uint32_t>(*v);
    }
    // unknown keys ignored
  }
  return Clamp(c);
}

std::string SerializeConfig(const Config& c) {
  std::ostringstream o;
  o << "# TetherDisplay host config\n"
    << "device_port=" << c.device_port << "\n"
    << "codec=" << (c.use_hevc ? "hevc" : "h264") << "\n"
    << "target_bitrate_kbps=" << c.target_bitrate_kbps << "\n"
    << "max_bitrate_kbps=" << c.max_bitrate_kbps << "\n"
    << "fps=" << c.fps << "\n";
  return o.str();
}

bool SaveConfig(const Config& c, const std::string& path) {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << SerializeConfig(c);
  return f.good();
}

std::optional<Config> LoadConfig(const std::string& path) {
  std::ifstream f(path);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return ParseConfig(ss.str());
}

}  // namespace td::core
