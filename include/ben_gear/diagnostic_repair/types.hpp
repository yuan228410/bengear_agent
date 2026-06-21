#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/diagnostic_context/types.hpp"
#include "ben_gear/domain/result.hpp"

#include <string>

namespace ben_gear::diagnostic_repair {

struct RepairPlanRequest {
    diagnostic_context::RepairContextRequest context;
};

struct RepairPlanResult {
    int diagnostic_count = 0;
    int plan_count = 0;
    bool truncated = false;
    Json summary = Json::object();
    Json plans = Json::array();
};

struct RepairPatchPreviewRequest {
    RepairPlanRequest plan_request;
    std::string unified_diff;
    std::string plan_id;
    int max_diff_bytes = 200 * 1024;
};

struct RepairPatchPreviewResult {
    int diagnostic_count = 0;
    int plan_count = 0;
    std::string selected_plan_id;
    Json repair_plan = Json::object();
    Json patch_preview = Json::object();
    Json candidate_file_match = Json::object();
    Json safety = Json::object();
    Json notes = Json::array();
};

domain::AppResult<RepairPlanRequest> repair_plan_request_from_json(const Json& request);
domain::AppResult<RepairPatchPreviewRequest> repair_patch_preview_request_from_json(const Json& request);
Json to_json(const RepairPlanResult& result);
Json to_json(const RepairPatchPreviewResult& result);

} // namespace ben_gear::diagnostic_repair
