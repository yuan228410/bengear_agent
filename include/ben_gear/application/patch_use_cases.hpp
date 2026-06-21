#pragma once

#include "ben_gear/application/request_context.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/domain/result.hpp"
#include "ben_gear/patch/types.hpp"

#include <string>

namespace ben_gear::application {

struct PatchPreviewQuery {
    RequestContext request;
    std::string unified_diff;
};

class PatchUseCases {
public:
    explicit PatchUseCases(const WorkspaceResolver& workspace_resolver);

    domain::AppResult<patch::PatchPreview> preview_patch(const PatchPreviewQuery& query) const;

private:
    const WorkspaceResolver& workspace_resolver_;
};

} // namespace ben_gear::application
