#pragma once

/// Stub: 诊断修复工作流服务
/// TODO: 在完整实现 DiagnosticRepairWorkflowService 时替换

#include "base/utils/json.hpp"
#include "base/container/string.hpp"
#include "base/domain/errors.hpp"
#include "application/workspace_resolver.hpp"
#include "application/command_governance.hpp"
#include "workspace/types.hpp"

namespace ben_gear::diagnostic_repair {

struct RepairWorkflowRequest {
    std::string workspace;
    std::string session_id;
    std::string username;
};

struct RepairWorkflowResult {
    bool success = false;
    std::string summary;
};

inline domain::AppResult<RepairWorkflowRequest> repair_workflow_request_from_json(const Json& json) {
    RepairWorkflowRequest req;
    req.workspace = json.value("workspace", "");
    req.session_id = json.value("session_id", "");
    req.username = json.value("username", "");
    if (req.workspace.empty() || req.session_id.empty())
        return domain::AppResult<RepairWorkflowRequest>::failure(
            domain::AppError::invalid_argument(
                base::container::String("invalid_request"),
                base::container::String("workspace and session_id are required")));
    return domain::AppResult<RepairWorkflowRequest>::success(std::move(req));
}

inline Json to_json(const RepairWorkflowResult& result) {
    Json j;
    j["success"] = result.success;
    j["summary"] = result.summary;
    return j;
}

class DiagnosticRepairWorkflowService {
public:
    DiagnosticRepairWorkflowService(
        const application::WorkspaceResolver& resolver)
        : DiagnosticRepairWorkflowService(resolver, application::CommandPipeline{}) {}

    DiagnosticRepairWorkflowService(
        const application::WorkspaceResolver& resolver,
        const application::CommandPipeline& pipeline)
        : resolver_(resolver), pipeline_(pipeline) {}

    domain::AppResult<RepairWorkflowResult> repair_workflow(const RepairWorkflowRequest& req) const {
        (void)req;
        RepairWorkflowResult result;
        result.success = true;
        result.summary = "repair workflow stub";
        return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
    }

private:
    const application::WorkspaceResolver& resolver_;
    const application::CommandPipeline& pipeline_;
};

} // namespace ben_gear::diagnostic_repair
