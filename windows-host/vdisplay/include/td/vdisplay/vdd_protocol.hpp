// Parsec VDD control constants — transcribed verbatim from nomi-san/parsec-vdd core/parsec-vdd.h
// (MIT wrapper that drives Parsec's proprietary, pre-signed IddCx driver; see docs/THIRD_PARTY.md,
// Option A1, R6-approved). This header is pure data + pure helpers (no Win32), so it is unit-testable
// on any platform. The numeric IOCTL codes and GUID below are the contract with the installed driver;
// do not invent or "tidy" them.
#ifndef TD_VDISPLAY_VDD_PROTOCOL_HPP
#define TD_VDISPLAY_VDD_PROTOCOL_HPP

#include <cstdint>

namespace td::vdisplay::parsec {

// PnP hardware id of the Parsec virtual display adapter.
inline constexpr const char* kHardwareId = "Root\\Parsec\\VDA";

// Device-interface GUID {00b41627-04c4-429e-a26e-0265cf50c8fa}. Laid out to match the Win32 GUID
// struct (Data1 u32, Data2/Data3 u16, Data4 8 bytes) so it can be reinterpreted there directly.
struct Guid {
  std::uint32_t data1;
  std::uint16_t data2;
  std::uint16_t data3;
  std::uint8_t  data4[8];
};
inline constexpr Guid kAdapterGuid{
    0x00b41627, 0x04c4, 0x429e, {0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa}};

// IOCTL control codes, each CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + N, METHOD_BUFFERED, access).
inline constexpr std::uint32_t kIoctlAdd     = 0x0022e004;  // add a display -> returns its index
inline constexpr std::uint32_t kIoctlRemove  = 0x0022a008;  // remove a display by (encoded) index
inline constexpr std::uint32_t kIoctlUpdate  = 0x0022a00c;  // keep-alive / commit pending changes
inline constexpr std::uint32_t kIoctlVersion = 0x0022e010;  // returns driver minor version

inline constexpr int kMaxDisplays = 8;

// The driver drops any display not refreshed within ~100 ms; ping comfortably under that bound.
inline constexpr int kKeepAliveIntervalMs = 90;

// REMOVE expects the display index as a byte-swapped UINT16, exactly as the reference wrapper builds
// it: `UINT16 indexData = ((index & 0xFF) << 8) | ((index >> 8) & 0xFF);`. Kept as a pure function so
// the (easy-to-get-wrong) swap is covered by a unit test rather than buried in Win32 code.
inline constexpr std::uint16_t EncodeRemoveIndex(int index) {
  return static_cast<std::uint16_t>(((index & 0xFF) << 8) | ((index >> 8) & 0xFF));
}

}  // namespace td::vdisplay::parsec

#endif  // TD_VDISPLAY_VDD_PROTOCOL_HPP
