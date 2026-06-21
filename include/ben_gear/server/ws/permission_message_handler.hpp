#pragma once

#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/server/ws/handler.hpp"
#include "ben_gear/server/ws/protocol.hpp"

#include <memory>

namespace ben_gear::server {

bool handle_permission_ws_message(SessionPool& session_pool,
                                  std::shared_ptr<WsHandler> ws,
                                  const container::String& username,
                                  const container::String& workspace,
                                  const WsMessage& msg);

} // namespace ben_gear::server
