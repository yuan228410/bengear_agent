#pragma once

#include "application/workspace_resolver.hpp"
#include "base/utils/json.hpp"
#include "server/session/pool.hpp"

namespace ben_gear::server::composition {

struct CommandApiCompositionContext {
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

} // namespace ben_gear::server::composition
