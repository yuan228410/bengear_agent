#pragma once

#include "server/core/router.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/db_api.hpp"
#include "server/api/memory_api.hpp"
#include "server/api/inspect_api.hpp"
#include "server/api/config_edit_api.hpp"
#include "server/api/agent_api.hpp"
#include "server/api/workspace_types.hpp"

#include <functional>
#include <memory>

namespace ben_gear::server {

class SessionPool;  // 前向声明，实现在 server/session/pool.hpp

void register_api_routes(Router& router,
                          std::shared_ptr<SessionService> session_svc,
                          std::shared_ptr<ConfigService> config_svc,
                          std::shared_ptr<WorkspaceService> ws_svc,
                          std::shared_ptr<McpService> mcp_svc,
                          std::shared_ptr<FileService> file_svc,
                          std::shared_ptr<workspace::HistoryDB> history_db,
                          const workspace::WorkspaceResolver& resolver,
                          SessionPool& session_pool,
                          std::function<bool()> reload_callback = {});

} // namespace ben_gear::server
