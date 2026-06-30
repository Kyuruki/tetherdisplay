// usbmux plist messages (M2) — build Listen/Connect requests and parse Result/Attached replies.
// Pure, portable (no sockets/Win32): unit-tested anywhere. Clean-room from the public protocol.
#ifndef TD_USBMUX_MESSAGES_HPP
#define TD_USBMUX_MESSAGES_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace td::usbmux {

// A device the daemon reported via an "Attached" event.
struct AttachedDevice {
  int         device_id = 0;        // pass this to Connect
  std::string serial;               // UDID
  std::string connection_type;      // "USB" or "Network"
  bool operator==(const AttachedDevice&) const = default;
};

// Request builders. Every request carries the boilerplate keys AMDS expects
// (BundleID, ClientVersionString, ProgName, kLibUSBMuxVersion=3).
std::string BuildListenPlist(std::string_view prog_name = "TetherDisplay");
// `device_port` is the plain host-order TCP port the iOS app listens on; this function applies the
// required big-endian (htons) swap when writing the PortNumber key.
std::string BuildConnectPlist(int device_id, std::uint16_t device_port,
                              std::string_view prog_name = "TetherDisplay");

// Minimal flat plist field extraction (value immediately follows its <key>). Sufficient for the
// well-formed plists usbmuxd emits; not a general plist parser.
std::optional<std::string> PlistGetString(std::string_view xml, std::string_view key);
std::optional<long long>   PlistGetInteger(std::string_view xml, std::string_view key);

// Convenience parsers over the above.
std::string                   ParseMessageType(std::string_view xml);   // "" if absent
std::optional<int>            ParseResultNumber(std::string_view xml);   // the Result.Number
std::optional<AttachedDevice> ParseAttached(std::string_view xml);       // nullopt if not an Attached

}  // namespace td::usbmux

#endif  // TD_USBMUX_MESSAGES_HPP
