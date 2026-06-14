#pragma once
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

namespace ben_gear::server {
void register_config_routes(Router& router, ConfigService& config_svc, WorkspaceService& ws_svc);
} // namespace ben_gear::server
