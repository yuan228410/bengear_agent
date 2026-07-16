#pragma once
#include "server/core/router.hpp"
#include "server/api/config_types.hpp"
#include "server/api/workspace_types.hpp"

#include <memory>

namespace ben_gear::server {
void register_config_routes(Router& router, std::shared_ptr<ConfigService> config_svc, std::shared_ptr<WorkspaceService> ws_svc);
} // namespace ben_gear::server
