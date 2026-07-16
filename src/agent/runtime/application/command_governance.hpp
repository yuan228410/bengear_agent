#pragma once

#include "agent/runtime/application/command.hpp"
#include "agent/runtime/application/command_pipeline.hpp"
#include "agent/runtime/application/runtime_execution.hpp"
#include "base/utils/json.hpp"
#include "domain/result.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace ben_gear::application {

namespace container = base::container;

using CheckToolPermissionFn = std::function<Json(const std::string& workspace,
                                                 const std::string& session_id,
                                                 const std::string& username,
                                                 std::string_view tool_name,
                                                 const Json& arguments)>;

using CreateCommandCheckpointFn = std::function<domain::AppResult<void>(const CommandDescriptor& command)>;

using AppendAuditEventFn = std::function<Json(const std::string& workspace,
                                              const std::string& session_id,
                                              const std::string& username,
                                              const std::string& category,
                                              const std::string& action,
                                              const Json& details)>;

using AppendRuntimeExecutionFn = std::function<Json(const std::string& workspace,
                                                    const std::string& session_id,
                                                    const std::string& username,
                                                    const Json& execution)>;

struct CommandGovernanceConfig {
    CheckToolPermissionFn check_permission = {};
    CreateCommandCheckpointFn create_checkpoint = {};
    AppendAuditEventFn append_audit_event = {};
    AppendRuntimeExecutionFn append_runtime_execution = {};
};

Json command_paths_json(const CommandDescriptor& command);
std::string command_risk_name(CommandRisk risk);
std::string command_tool_name(const CommandDescriptor& command);
Json command_permission_arguments(const CommandDescriptor& command);
core::PermissionGateRef command_permission_gate(const CommandDescriptor& command);
core::RuntimeBoundary command_runtime_boundary(const CommandDescriptor& command);
CommandDescriptor safe_code_change_command(const CommandDescriptor& patch_command,
                                           std::string_view test_command,
                                           std::string_view test_cwd,
                                           int test_timeout_seconds,
                                           int test_max_output_bytes);

CommandPipeline make_command_pipeline(CommandGovernanceConfig config);
RuntimeExecutionKernel make_runtime_execution_kernel(CommandGovernanceConfig config);
ExecutionRequest command_execution_request(const CommandDescriptor& command, bool dry_run = false);

} // namespace ben_gear::application
