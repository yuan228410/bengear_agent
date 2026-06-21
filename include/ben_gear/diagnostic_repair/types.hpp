#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <string>

namespace ben_gear::diagnostic_repair {

struct RepairPlanResult {
    int diagnostic_count = 0;
    int plan_count = 0;
    bool truncated = false;
    Json summary = Json::object();
    Json plans = Json::array();
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

Json to_json(const RepairPlanResult& result);
Json to_json(const RepairPatchPreviewResult& result);

} // namespace ben_gear::diagnostic_repair
