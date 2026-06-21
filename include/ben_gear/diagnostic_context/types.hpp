#pragma once

#include "ben_gear/base/utils/json.hpp"

namespace ben_gear::diagnostic_context {

struct RepairContextResult {
    int diagnostic_count = 0;
    bool truncated = false;
    Json contexts = Json::array();
    Json files = Json::array();
};

Json to_json(const RepairContextResult& result);

} // namespace ben_gear::diagnostic_context
