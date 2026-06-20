#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/code_intel/code_intel_service.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <memory>

namespace ben_gear::diagnostic_context {

class DiagnosticContextService {
public:
    explicit DiagnosticContextService(workspace::WorkspaceContext ws_ctx,
                                      std::shared_ptr<code_intel::CodeIntelService> code_intel_service = nullptr);

    Json repair_context(const Json& request) const;

private:
    std::filesystem::path project_root() const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<code_intel::CodeIntelService> code_intel_service_;
};

} // namespace ben_gear::diagnostic_context
