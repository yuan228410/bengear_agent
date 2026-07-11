#pragma once

#include "base/utils/json.hpp"
#include "intelligence/diagnostic_context/types.hpp"
#include "base/domain/result.hpp"

#include <string>

namespace ben_gear::diagnostic_repair {

struct RepairPlanRequest {
    diagnostic_context::RepairContextRequest context;
    std::string failure_category;
    std::string command;
    int timeout_seconds = 120;
    int max_output_bytes = 60000;
};

struct RepairPlanResult {
    int diagnostic_count = 0;
    int plan_count = 0;
    bool truncated = false;
    Json summary = Json::object();
    Json recommended_rerun = Json::object();
    Json plans = Json::array();
};

struct RepairPatchPreviewRequest {
    RepairPlanRequest plan_request;
    std::string unified_diff;
    std::string plan_id;
    int max_diff_bytes = 200 * 1024;
};

struct RepairPatchDraftRequest {
    RepairPatchPreviewRequest preview_request;
    Json code_context = Json::object();
    std::string draft_hint;
    std::string missing_source;
    std::string cmake_target;
    int max_diff_bytes = 200 * 1024;
};

struct RepairPatchDraftResult {
    bool drafted = false;
    std::string status = "no_draft";
    std::string draft_provider = "deterministic";
    std::string draft_rule;
    std::string draft_id;
    std::string plan_id;
    std::string context_pack_id;
    std::string unified_diff;
    Json touched_files = Json::array();
    Json rationale = Json::array();
    int confidence = 0;
    std::string risk_level = "unknown";
    Json validation_notes = Json::array();
    Json no_draft_reasons = Json::array();
    Json preview = Json::object();
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
domain::AppResult<RepairPatchDraftRequest> repair_patch_draft_request_from_json(const Json& request);
Json to_json(const RepairPlanResult& result);
Json to_json(const RepairPatchPreviewResult& result);
Json to_json(const RepairPatchDraftResult& result);

} // namespace ben_gear::diagnostic_repair
