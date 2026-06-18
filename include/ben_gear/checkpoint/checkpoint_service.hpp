#pragma once

#include "ben_gear/checkpoint/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ben_gear::checkpoint {

class CheckpointService {
public:
    explicit CheckpointService(workspace::WorkspaceContext ws_ctx);

    Json create(const std::vector<std::string>& paths, const std::string& description = {}) const;
    Json list() const;
    Json read(std::string_view checkpoint_id) const;
    Json restore(std::string_view checkpoint_id,
                 const std::vector<std::string>& paths = {},
                 bool force = false) const;
    Json remove(std::string_view checkpoint_id) const;

private:
    std::filesystem::path project_root() const;
    std::filesystem::path base_dir() const;
    std::filesystem::path checkpoint_path(std::string_view checkpoint_id) const;
    bool validate_path(const std::string& input, std::string& normalized, std::string& error) const;
    std::optional<CheckpointRecord> load(std::string_view checkpoint_id, std::string& error) const;
    bool save(const CheckpointRecord& record, std::string& error) const;

    workspace::WorkspaceContext ws_ctx_;
};

} // namespace ben_gear::checkpoint
