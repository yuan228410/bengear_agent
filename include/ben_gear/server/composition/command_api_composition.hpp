#pragma once

#include "ben_gear/application/command_governance.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/audit/audit_store.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/checkpoint/checkpoint_service.hpp"
#include "ben_gear/git/git_service.hpp"
#include "ben_gear/patch/diff_parser.hpp"
#include "ben_gear/server/api/deps.hpp"
#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/test_loop/test_loop_service.hpp"

namespace ben_gear::server::composition {

struct CommandApiCompositionContext {
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

application::CommandPipeline make_server_command_pipeline(CommandApiCompositionContext context);
GitApiService make_git_api_service(CommandApiCompositionContext context);
PermissionApiService make_permission_api_service(CommandApiCompositionContext context);
CheckpointApiService make_checkpoint_api_service(CommandApiCompositionContext context);
TestLoopApiService make_test_loop_api_service(CommandApiCompositionContext context);

} // namespace ben_gear::server::composition
