#pragma once

#include "server/session/pool.hpp"
#include "server/ws/handler.hpp"
#include "server/ws/protocol.hpp"

#include <memory>

namespace ben_gear::server {

bool handle_permission_ws_message(SessionPool& session_pool,
                                  std::shared_ptr<WsHandler> ws,
                                  const container::String& username,
                                  const container::String& workspace,
                                  const WsMessage& msg);

} // namespace ben_gear::server
