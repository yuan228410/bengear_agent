#pragma once

#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/application/request_context.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/diagnostic_repair/types.hpp"
#include "ben_gear/domain/result.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ben_gear::diagnostic_repair {

struct RepairWorkflowPatchCandidate {
    std::string id;
    std::string unified_diff;
    std::string description;
};

struct RepairWorkflowRequest {
    application::RequestContext request;
    RepairPlanRequest plan_request;
    std::vector<RepairWorkflowPatchCandidate> patch_candidates;
    int max_iterations = 1;
    bool apply_patch = true;
    bool rerun_tests = true;
};

struct RepairWorkflowResult {
    bool success = false;
    std::string status;
    int iterations = 0;
    Json summary = Json::object();
    Json repair_plan = Json::object();
    Json attempts = Json::array();
    Json final_test_result = Json::object();
    Json safety = Json::object();
};

class DiagnosticRepairWorkflowService {
public:
    explicit DiagnosticRepairWorkflowService(const application::WorkspaceResolver& workspace_resolver,
                                             application::CommandPipeline command_pipeline = application::CommandPipeline());

    domain::AppResult<RepairWorkflowResult> repair_workflow(const RepairWorkflowRequest& request) const;

private:
    const application::WorkspaceResolver& workspace_resolver_;
    application::CommandPipeline command_pipeline_;
};

domain::AppResult<RepairWorkflowRequest> repair_workflow_request_from_json(const Json& request);
Json to_json(const RepairWorkflowResult& result);

} // namespace ben_gear::diagnostic_repair
