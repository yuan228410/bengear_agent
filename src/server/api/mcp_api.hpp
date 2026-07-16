#pragma once
#include "server/core/router.hpp"
#include "server/api/mcp_types.hpp"

#include <memory>

namespace ben_gear::server {
void register_mcp_routes(Router& router, std::shared_ptr<McpService> service);
} // namespace ben_gear::server
