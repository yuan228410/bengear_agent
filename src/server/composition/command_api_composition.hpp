#pragma once

#include "application/command_governance.hpp"
#include "application/patch_use_cases.hpp"
#include "application/safe_code_change_service.hpp"
#include "application/workspace_resolver.hpp"
#include "capabilities/audit/audit_store.hpp"
#include "base/utils/json.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "capabilities/git/git_service.hpp"
#include "capabilities/patch/diff_parser.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "server/api/checkpoint_types.hpp"
#include "server/api/git_types.hpp"
#include "server/api/patch_types.hpp"
#include "server/api/permission_types.hpp"
#include "server/api/test_loop_types.hpp"
#include "server/session/pool.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"

namespace ben_gear::server::composition {

struct CommandApiCompositionContext {
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

application::CommandPipeline make_server_command_pipeline(CommandApiCompositionContext context);
GitApiService make_git_api_service(CommandApiCompositionContext context);
PermissionApiService make_permission_api_service(CommandApiCompositionContext context);
CheckpointApiService make_checkpoint_api_service(CommandApiCompositionContext context);
PatchApiService make_patch_api_service(CommandApiCompositionContext context);
TestLoopApiService make_test_loop_api_service(CommandApiCompositionContext context);

} // namespace ben_gear::server::composition
