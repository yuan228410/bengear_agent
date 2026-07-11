#pragma once
#include "server/core/router.hpp"
#include "server/api/config_types.hpp"
#include "server/api/workspace_types.hpp"

namespace ben_gear::server {
void register_config_routes(Router& router, ConfigService& config_svc, WorkspaceService& ws_svc);
} // namespace ben_gear::server
