// usbmux plist message build/parse — portable implementation. See usbmux_messages.hpp.
#include "td/usbmux/usbmux_messages.hpp"

#include "td/usbmux/usbmux_protocol.hpp"

#include <charconv>

namespace td::usbmux {
namespace {

constexpr const char* kPlistHeader =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n<dict>\n";
constexpr const char* kPlistFooter = "</dict>\n</plist>\n";

// The boilerplate keys pymobiledevice3/AMDS expect on every request.
std::string Boilerplate(std::string_view prog_name) {
  std::string s;
  s += "  <key>BundleID</key><string>com.tetherdisplay.host</string>\n";
  s += "  <key>ClientVersionString</key><string>tetherdisplay-usbmux</string>\n";
  s += "  <key>ProgName</key><string>";
  s.append(prog_name);
  s += "</string>\n";
  s += "  <key>kLibUSBMuxVersion</key><integer>3</integer>\n";
  return s;
}

// Find the inner text of the first <tag>...</tag> that appears AFTER "<key>key</key>" and before the
// next <key>. Assumes the queried key is unique with a scalar value (true for usbmuxd's Attached/Result
// plists); a key duplicated with differing values would return only the first occurrence.
std::optional<std::string> value_after_key(std::string_view xml, std::string_view key,
                                           std::string_view tag) {
  std::string key_marker = "<key>";
  key_marker.append(key);
  key_marker += "</key>";
  const auto kpos = xml.find(key_marker);
  if (kpos == std::string_view::npos) return std::nullopt;

  std::string open = "<";
  open.append(tag);
  open += ">";
  std::string close = "</";
  close.append(tag);
  close += ">";
  const auto kend = kpos + key_marker.size();
  const auto opos = xml.find(open, kend);
  if (opos == std::string_view::npos) return std::nullopt;
  // The value must belong to THIS key: it has to appear before the next <key> starts, otherwise we'd
  // wrongly pick up a later key's value of the requested type (e.g. asking MessageType as an integer).
  const auto next_key = xml.find("<key>", kend);
  if (next_key != std::string_view::npos && opos > next_key) return std::nullopt;
  const auto vstart = opos + open.size();
  const auto cpos = xml.find(close, vstart);
  if (cpos == std::string_view::npos) return std::nullopt;
  return std::string(xml.substr(vstart, cpos - vstart));
}

}  // namespace

std::string BuildListenPlist(std::string_view prog_name) {
  std::string s = kPlistHeader;
  s += "  <key>MessageType</key><string>Listen</string>\n";
  s += Boilerplate(prog_name);
  s += kPlistFooter;
  return s;
}

std::string BuildConnectPlist(int device_id, std::uint16_t device_port, std::string_view prog_name) {
  const std::uint16_t be_port = EncodePortBigEndian(device_port);
  std::string s = kPlistHeader;
  s += "  <key>MessageType</key><string>Connect</string>\n";
  s += "  <key>DeviceID</key><integer>" + std::to_string(device_id) + "</integer>\n";
  s += "  <key>PortNumber</key><integer>" + std::to_string(be_port) + "</integer>\n";
  s += Boilerplate(prog_name);
  s += kPlistFooter;
  return s;
}

std::optional<std::string> PlistGetString(std::string_view xml, std::string_view key) {
  return value_after_key(xml, key, "string");
}

std::optional<long long> PlistGetInteger(std::string_view xml, std::string_view key) {
  auto raw = value_after_key(xml, key, "integer");
  if (!raw) return std::nullopt;
  long long v = 0;
  const auto* begin = raw->data();
  const auto* end = begin + raw->size();
  auto [ptr, ec] = std::from_chars(begin, end, v);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return v;
}

std::string ParseMessageType(std::string_view xml) {
  return PlistGetString(xml, "MessageType").value_or("");
}

std::optional<int> ParseResultNumber(std::string_view xml) {
  auto n = PlistGetInteger(xml, "Number");
  if (!n) return std::nullopt;
  return static_cast<int>(*n);
}

std::optional<AttachedDevice> ParseAttached(std::string_view xml) {
  if (ParseMessageType(xml) != "Attached") return std::nullopt;
  auto id = PlistGetInteger(xml, "DeviceID");
  if (!id) return std::nullopt;
  AttachedDevice d;
  d.device_id = static_cast<int>(*id);
  d.serial = PlistGetString(xml, "SerialNumber").value_or("");
  d.connection_type = PlistGetString(xml, "ConnectionType").value_or("");
  return d;
}

}  // namespace td::usbmux
