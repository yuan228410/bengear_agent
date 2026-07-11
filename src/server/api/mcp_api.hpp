#pragma once
#include "server/core/router.hpp"
#include "server/api/mcp_types.hpp"

namespace ben_gear::server {
void register_mcp_routes(Router& router, McpService& service);
} // namespace ben_gear::server
