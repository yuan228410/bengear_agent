#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct DiagnosticContextApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_context;
};

struct DiagnosticRepairApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_plan;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_patch_preview;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_patch_draft;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_workflow;
};

} // namespace ben_gear::server
