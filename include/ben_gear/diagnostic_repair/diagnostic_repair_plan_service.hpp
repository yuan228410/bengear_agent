#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/diagnostic_context/diagnostic_context_service.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <memory>

namespace ben_gear::diagnostic_repair {

class DiagnosticRepairPlanService {
public:
    explicit DiagnosticRepairPlanService(
        workspace::WorkspaceContext ws_ctx,
        std::shared_ptr<diagnostic_context::DiagnosticContextService> context_service = nullptr);

    Json repair_plan(const Json& request) const;

private:
    std::filesystem::path project_root() const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> context_service_;
};

} // namespace ben_gear::diagnostic_repair
