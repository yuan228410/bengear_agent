#pragma once

#include "ben_gear/patch/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::patch {

class ChangeStore {
public:
    explicit ChangeStore(workspace::WorkspaceContext ws_ctx);

    std::filesystem::path change_path(std::string_view change_id) const;
    bool save(const ChangeRecord& record, std::string& error) const;
    std::optional<ChangeRecord> load(std::string_view change_id, std::string& error) const;
    std::vector<ChangeRecord> list(std::string& error) const;

private:
    std::filesystem::path base_dir() const;

    workspace::WorkspaceContext ws_ctx_;
};

} // namespace ben_gear::patch
