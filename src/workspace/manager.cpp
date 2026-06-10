#include "ben_gear/workspace/manager.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace ben_gear::workspace {

WorkspaceManager::WorkspaceManager(const std::filesystem::path& user_dir)
    : user_dir_(user_dir), workspaces_dir_(user_dir / "workspaces") {
    std::filesystem::create_directories(workspaces_dir_);
    ensure_default();
}

std::optional<WorkspaceMeta> WorkspaceManager::create(
    const container::String& name,
    const container::String& project_path) {
    auto name_str = std::string(name.data(), name.size());
    if (!is_valid_workspace_name(name_str)) {
        log::error_fmt("invalid workspace name: {}", name_str);
        return std::nullopt;
    }
    auto dir = workspaces_dir_ / name_str;
    if (std::filesystem::exists(dir)) {
        log::warn_fmt("workspace already exists: {}", name_str);
        return std::nullopt;
    }
    return create_workspace_dir(name, project_path);
}

std::optional<WorkspaceMeta> WorkspaceManager::get(
    const container::String& name) const {
    auto name_str = std::string(name.data(), name.size());
    auto dir = workspaces_dir_ / name_str;
    if (!std::filesystem::exists(dir)) return std::nullopt;
    return load_meta(name, dir);
}

container::Vector<WorkspaceMeta> WorkspaceManager::list_all() const {
    container::Vector<WorkspaceMeta> result;
    if (!std::filesystem::exists(workspaces_dir_)) return result;

    for (const auto& entry :
         std::filesystem::directory_iterator(workspaces_dir_)) {
        if (!entry.is_directory()) continue;
        auto name = entry.path().filename().string();
        if (name.starts_with(".")) continue;
        auto meta =
            load_meta(container::String(name.c_str()), entry.path());
        if (meta) result.push_back(*meta);
    }
    return result;
}

container::Vector<WorkspaceMeta> WorkspaceManager::list_removed() const {
    container::Vector<WorkspaceMeta> result;
    if (!std::filesystem::exists(workspaces_dir_)) return result;

    for (const auto& entry :
         std::filesystem::directory_iterator(workspaces_dir_)) {
        if (!entry.is_directory()) continue;
        auto dirname = entry.path().filename().string();
        if (!dirname.starts_with(".")) continue;
        if (dirname.find(".removed_") == std::string::npos) continue;

        auto name = dirname.substr(1);
        auto removed_pos = name.find(".removed_");
        if (removed_pos != std::string::npos) {
            name = name.substr(0, removed_pos);
        }

        auto meta =
            load_meta(container::String(name.c_str()), entry.path());
        if (meta) {
            meta->deleted = true;
            result.push_back(*meta);
        } else {
            WorkspaceMeta fallback;
            fallback.name = container::String(name.c_str());
            fallback.ws_dir = entry.path();
            fallback.deleted = true;
            result.push_back(fallback);
        }
    }
    return result;
}

bool WorkspaceManager::remove(const container::String& name) {
    auto name_str = std::string(name.data(), name.size());
    if (name_str == "default") {
        log::warn_fmt("cannot remove default workspace");
        return false;
    }

    auto dir = workspaces_dir_ / name_str;
    if (!std::filesystem::exists(dir)) return false;

    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                  now.time_since_epoch())
                  .count();
    auto removed_name = "." + name_str + ".removed_" + std::to_string(ts);
    auto removed_dir = workspaces_dir_ / removed_name;

    std::error_code ec;
    std::filesystem::rename(dir, removed_dir, ec);
    if (ec) {
        log::error_fmt("workspace remove failed: {}, error={}", name_str,
                       ec.message());
        return false;
    }

    log::info_fmt("workspace removed: {} -> {}", name_str, removed_name);
    return true;
}

bool WorkspaceManager::restore(const container::String& name) {
    auto name_str = std::string(name.data(), name.size());

    if (!std::filesystem::exists(workspaces_dir_)) return false;

    for (const auto& entry :
         std::filesystem::directory_iterator(workspaces_dir_)) {
        if (!entry.is_directory()) continue;
        auto dirname = entry.path().filename().string();
        auto prefix = "." + name_str + ".removed_";
        if (!dirname.starts_with(prefix)) continue;

        auto target = workspaces_dir_ / name_str;
        std::error_code ec;
        std::filesystem::rename(entry.path(), target, ec);
        if (ec) {
            log::error_fmt("workspace restore failed: {}, error={}",
                           name_str, ec.message());
            return false;
        }

        log::info_fmt("workspace restored: {} -> {}", dirname, name_str);
        return true;
    }
    return false;
}

TierPaths WorkspaceManager::tier_paths_for(
    const container::String& ws_name) const {
    auto root = support::data_directory();
    auto ws_str = std::string(ws_name.data(), ws_name.size());
    return {root, user_dir_, workspaces_dir_ / ws_str};
}

void WorkspaceManager::ensure_default() {
    auto default_dir = workspaces_dir_ / "default";
    if (!std::filesystem::exists(default_dir)) {
        create_workspace_dir(container::String("default"), {});
        log::info_fmt("default workspace created");
    }
}

WorkspaceMeta WorkspaceManager::create_workspace_dir(
    const container::String& name,
    const container::String& project_path) {
    auto name_str = std::string(name.data(), name.size());

    if (name_str.find('/') != std::string::npos ||
        name_str.find('\\') != std::string::npos ||
        name_str.find("..") != std::string::npos) {
        log::error_fmt("invalid workspace name (path traversal): {}",
                       name_str);
        return {name, project_path, {}, false};
    }

    auto dir = workspaces_dir_ / name_str;
    std::filesystem::create_directories(dir);

    {
        auto meta_path = dir / "workspace.json";
        Json meta;
        meta["name"] = name_str;
        meta["project_path"] =
            std::string(project_path.data(), project_path.size());
        std::ofstream file(meta_path, std::ios::binary | std::ios::trunc);
        file << meta.dump(2);
    }

    log::info_fmt("workspace created: {}", name_str);
    return {name, project_path, dir, false};
}

std::optional<WorkspaceMeta> WorkspaceManager::load_meta(
    const container::String& name,
    const std::filesystem::path& dir) const {
    auto meta_path = dir / "workspace.json";
    if (std::filesystem::exists(meta_path)) {
        std::ifstream file(meta_path, std::ios::binary);
        if (file) {
            std::string content{std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>()};
            std::string err;
            auto json = parse_json(content, err);
            if (err.empty()) {
                return WorkspaceMeta{
                    container::String(json.value("name", "").c_str()),
                    container::String(
                        json.value("project_path", "").c_str()),
                    dir,
                    false};
            }
        }
    }
    return WorkspaceMeta{name, {}, dir, false};
}

bool WorkspaceManager::is_valid_workspace_name(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == '.' || c == '\0' || c == ':')
            return false;
    }
    if (name == "..") return false;
    return true;
}

}  // namespace ben_gear::workspace
