// Host control-plane logic — portable. See control_plane.hpp.
#include "td/core/control_plane.hpp"

#include <variant>

namespace td::core {

ControlActions HandleControlMessage(const proto::Message& msg) {
  ControlActions a;
  std::visit(
      [&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, proto::KeyframeRequest>) {
          a.force_idr = true;
        } else if constexpr (std::is_same_v<T, proto::Ping>) {
          a.send_pong = true;
          a.pong_token = m.token;
        } else if constexpr (std::is_same_v<T, proto::Hello>) {
          a.have_client_caps = true;
          a.client_caps = m;
        } else if constexpr (std::is_same_v<T, proto::FormatChange>) {
          a.format_change = true;
          a.new_format = m.format;
        } else if constexpr (std::is_same_v<T, proto::Disconnect>) {
          a.disconnect = true;
        }
        // Welcome / Pong / VideoFrame: the host sends these, so receiving them yields no action.
      },
      msg);
  return a;
}

}  // namespace td::core
