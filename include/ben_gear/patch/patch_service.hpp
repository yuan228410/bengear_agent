#pragma once

#include "ben_gear/domain/result.hpp"
#include "ben_gear/patch/change_store.hpp"
#include "ben_gear/patch/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::patch {

class PatchService {
public:
    explicit PatchService(workspace::WorkspaceContext ws_ctx);

    PatchPreview preview(std::string_view unified_diff) const;
    domain::AppResult<PatchValidatedPreviewResult> preview_validated(std::string_view unified_diff) const;
    domain::AppResult<PatchApplyResult> apply(std::string_view unified_diff, std::string_view description = {});
    domain::AppResult<PatchRevertResult> revert(std::string_view change_id, bool force = false);
    domain::AppResult<PatchListChangesResult> list_changes() const;
    domain::AppResult<PatchReadChangeResult> read_change(std::string_view change_id) const;

private:
    std::filesystem::path project_root() const;
    std::filesystem::path resolve_workspace_path(const std::filesystem::path& relative, std::string& error) const;
    std::string relative_display_path(const std::filesystem::path& path) const;

    workspace::WorkspaceContext ws_ctx_;
    ChangeStore store_;
};

} // namespace ben_gear::patch
