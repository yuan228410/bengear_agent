#pragma once

#include "ben_gear/application/command.hpp"
#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/domain/result.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace ben_gear::server {

namespace container = base::container;

using CheckToolPermissionFn = std::function<Json(const container::String& workspace,
                                                 const container::String& session_id,
                                                 const container::String& username,
                                                 std::string_view tool_name,
                                                 const Json& arguments)>;

using CreateCommandCheckpointFn = std::function<domain::AppResult<void>(const application::CommandDescriptor& command)>;

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

Json command_paths_json(const application::CommandDescriptor& command);
std::string command_risk_name(application::CommandRisk risk);
std::string command_tool_name(const application::CommandDescriptor& command);
Json command_permission_arguments(const application::CommandDescriptor& command);

application::CommandPipeline make_command_pipeline(CommandGovernanceConfig config);

} // namespace ben_gear::server
