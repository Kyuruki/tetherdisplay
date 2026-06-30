// Host control-plane logic (M4) — decides what to do with a §5 control message received FROM the iPad.
// Pure (depends only on the portable protocol module), so it is unit-tested in WSL. The Win32
// StreamingSession does the actual I/O (force an IDR, send a PONG, etc.) based on these decisions.
#ifndef TD_CORE_CONTROL_PLANE_HPP
#define TD_CORE_CONTROL_PLANE_HPP

#include "td/protocol/wire.hpp"

#include <cstdint>

namespace td::core {

// What the session should do in response to one inbound control message.
struct ControlActions {
  bool force_idr = false;                 // a KEYFRAME_REQUEST arrived → force an IDR on the next frame
  bool send_pong = false;                 // a PING arrived → reply PONG
  std::uint64_t pong_token = 0;           // the token to echo in that PONG
  bool have_client_caps = false;          // a HELLO arrived → client capabilities captured
  proto::Hello client_caps{};
  bool format_change = false;             // a FORMAT_CHANGE arrived (renegotiation request)
  proto::Welcome new_format{};
  bool disconnect = false;                // a DISCONNECT arrived → tear down
};

// Map an inbound message to the actions the host should take. Video/Welcome messages (which the host
// sends, not receives) and anything unexpected produce no actions.
ControlActions HandleControlMessage(const proto::Message& msg);

}  // namespace td::core

#endif  // TD_CORE_CONTROL_PLANE_HPP
