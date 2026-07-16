#pragma once

#include <memory>

#include "agent/runtime/application/workspace_resolver.hpp"
#include "base/config/settings.hpp"
#include "server/api/config_types.hpp"
#include "server/api/mcp_types.hpp"
#include "server/api/session_types.hpp"
#include "server/api/workspace_types.hpp"
#include "server/api/file_types.hpp"
#include "server/session/pool.hpp"

namespace ben_gear::server::composition {

struct BasicApiCompositionContext {
    config::Settings& settings;
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

std::shared_ptr<SessionService> make_session_api_service(BasicApiCompositionContext context);
std::shared_ptr<ConfigService> make_config_api_service(BasicApiCompositionContext context);
std::shared_ptr<WorkspaceService> make_workspace_api_service(BasicApiCompositionContext context);
std::shared_ptr<McpService> make_mcp_api_service();
std::shared_ptr<FileService> make_file_api_service();
} // namespace ben_gear::server::composition
