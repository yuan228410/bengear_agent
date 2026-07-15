#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct DiagnosticContextApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> repair_context;
};

struct DiagnosticRepairApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> repair_plan;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> repair_patch_preview;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> repair_patch_draft;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> repair_workflow;
};

} // namespace ben_gear::server
