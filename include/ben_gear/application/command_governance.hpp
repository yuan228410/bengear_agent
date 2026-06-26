#pragma once

#include "ben_gear/application/command.hpp"
#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/application/runtime_execution.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/domain/result.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace ben_gear::application {

namespace container = base::container;

using CheckToolPermissionFn = std::function<Json(const container::String& workspace,
                                                 const container::String& session_id,
                                                 const container::String& username,
                                                 std::string_view tool_name,
                                                 const Json& arguments)>;

using CreateCommandCheckpointFn = std::function<domain::AppResult<void>(const CommandDescriptor& command)>;

using AppendAuditEventFn = std::function<void(const container::String& workspace,
                                              const container::String& session_id,
                                              const container::String& username,
                                              const container::String& category,
                                              const container::String& action,
                                              const Json& details)>;

struct CommandGovernanceConfig {
    CheckToolPermissionFn check_permission;
    CreateCommandCheckpointFn create_checkpoint;
    AppendAuditEventFn append_audit_event;
};

Json command_paths_json(const CommandDescriptor& command);
std::string command_risk_name(CommandRisk risk);
std::string command_tool_name(const CommandDescriptor& command);
Json command_permission_arguments(const CommandDescriptor& command);
core::PermissionGateRef command_permission_gate(const CommandDescriptor& command);
core::RuntimeBoundary command_runtime_boundary(const CommandDescriptor& command);

CommandPipeline make_command_pipeline(CommandGovernanceConfig config);
RuntimeExecutionKernel make_runtime_execution_kernel(CommandGovernanceConfig config);
ExecutionRequest command_execution_request(const CommandDescriptor& command, bool dry_run = false);

} // namespace ben_gear::application
