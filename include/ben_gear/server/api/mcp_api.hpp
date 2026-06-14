#pragma once
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

namespace ben_gear::server {
void register_mcp_routes(Router& router, McpService& service);
} // namespace ben_gear::server
