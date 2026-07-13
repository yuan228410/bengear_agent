#pragma once

#include "base/utils/json.hpp"
#include "domain/result.hpp"
#include "capabilities/test_loop/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ben_gear::diagnostic_context {

struct RepairContextRequest {
    std::vector<test_loop::TestDiagnostic> diagnostics;
    std::string output;
    std::string cwd = ".";
    int context_lines = 5;
    int max_diagnostics = 20;
    std::int64_t max_file_bytes = 1024 * 1024;
    std::int64_t max_total_bytes = 60000;
    bool include_code_intel = true;
    std::string runtime_execution_id;
    Json runtime_execution = Json::object();
    Json code_context = Json::object();
};

struct RepairContextResult {
    int diagnostic_count = 0;
    bool truncated = false;
    Json contexts = Json::array();
    Json files = Json::array();
    Json runtime_execution = Json::object();
    Json code_context = Json::object();
};

domain::AppResult<RepairContextRequest> repair_context_request_from_json(const Json& request);
Json to_json(const RepairContextResult& result);

} // namespace ben_gear::diagnostic_context
