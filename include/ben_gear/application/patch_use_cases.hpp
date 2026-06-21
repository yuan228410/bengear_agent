#pragma once

#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/application/request_context.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/domain/result.hpp"
#include "ben_gear/patch/types.hpp"

#include <string>
#include <vector>

namespace ben_gear::application {

struct PatchPreviewQuery {
    RequestContext request;
    std::string unified_diff;
};

struct PatchApplyCommand {
    RequestContext request;
    std::string unified_diff;
    std::string description;
};

struct PatchRevertCommand {
    RequestContext request;
    std::string change_id;
    bool force = false;
};

using PatchApplyResult = patch::PatchApplyResult;
using PatchRevertResult = patch::PatchRevertResult;

class PatchUseCases {
public:
    explicit PatchUseCases(const WorkspaceResolver& workspace_resolver,
                           CommandPipeline command_pipeline = CommandPipeline());

    domain::AppResult<patch::PatchPreview> preview_patch(const PatchPreviewQuery& query) const;
    domain::AppResult<PatchApplyResult> apply_patch(const PatchApplyCommand& command) const;
    domain::AppResult<PatchRevertResult> revert_patch(const PatchRevertCommand& command) const;

private:
    const WorkspaceResolver& workspace_resolver_;
    CommandPipeline command_pipeline_;
};

} // namespace ben_gear::application
