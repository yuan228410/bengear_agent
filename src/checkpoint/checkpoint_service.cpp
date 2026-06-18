#include "ben_gear/checkpoint/checkpoint_service.hpp"

#include "ben_gear/workspace/uuid.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ben_gear::checkpoint {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string hash_content(std::string_view content) {
    auto hash = std::hash<std::string_view>{}(content);
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

std::string read_text(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "failed to read file";
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_text(const std::filesystem::path& path, std::string_view content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    auto tmp = path;
    tmp += ".checkpoint.tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "failed to write temp file";
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}};
}

} // namespace

Json to_json(const CheckpointFileRecord& file) {
    return Json{{"path", file.path},
                {"existed", file.existed},
                {"content", file.content},
                {"hash", file.hash},
                {"size", static_cast<std::uint64_t>(file.size)}};
}

Json to_json(const CheckpointRecord& record) {
    Json files = Json::array();
    for (const auto& file : record.files) files.push_back(to_json(file));
    return Json{{"checkpoint_id", record.checkpoint_id},
                {"session_id", record.session_id},
                {"description", record.description},
                {"created_at", record.created_at},
                {"files", files},
                {"restored", record.restored},
                {"restored_at", record.restored_at}};
}

CheckpointService::CheckpointService(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

std::filesystem::path CheckpointService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

std::filesystem::path CheckpointService::base_dir() const {
    auto session_id = ws_ctx_.session_id.empty() ? std::string("default") : to_std(ws_ctx_.session_id);
    return ws_ctx_.tier_paths.user_dir / "checkpoints" / session_id;
}

std::filesystem::path CheckpointService::checkpoint_path(std::string_view checkpoint_id) const {
    return base_dir() / (std::string(checkpoint_id) + ".json");
}

bool CheckpointService::validate_path(const std::string& input, std::string& normalized, std::string& error) const {
    if (input.empty()) {
        error = "path must be non-empty";
        return false;
    }
    std::filesystem::path path(input);
    if (path.is_absolute()) {
        error = "checkpoint paths must be relative to the workspace";
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            error = "checkpoint path escapes workspace";
            return false;
        }
    }
    normalized = path.generic_string();
    return true;
}

bool CheckpointService::save(const CheckpointRecord& record, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(base_dir(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    auto path = checkpoint_path(record.checkpoint_id);
    auto tmp = path;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "failed to write checkpoint";
        return false;
    }
    auto data = to_json(record).dump();
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

std::optional<CheckpointRecord> CheckpointService::load(std::string_view checkpoint_id, std::string& error) const {
    auto id = std::string(checkpoint_id);
    if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos) {
        error = "invalid checkpoint_id";
        return std::nullopt;
    }
    auto path = checkpoint_path(id);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "checkpoint not found";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    try {
        auto json = Json::parse(buffer.str());
        CheckpointRecord record;
        record.checkpoint_id = json.value("checkpoint_id", "");
        record.session_id = json.value("session_id", "");
        record.description = json.value("description", "");
        record.created_at = json.value("created_at", "");
        record.restored = json.value("restored", false);
        record.restored_at = json.value("restored_at", "");
        if (json.contains("files") && json["files"].is_array()) {
            for (const auto& item : json["files"]) {
                CheckpointFileRecord file;
                file.path = item.value("path", "");
                file.existed = item.value("existed", false);
                file.content = item.value("content", "");
                file.hash = item.value("hash", "");
                file.size = item.value("size", static_cast<std::uintmax_t>(0));
                record.files.push_back(std::move(file));
            }
        }
        return record;
    } catch (const std::exception& ex) {
        error = ex.what();
        return std::nullopt;
    }
}

Json CheckpointService::create(const std::vector<std::string>& paths, const std::string& description) const {
    if (paths.empty()) return error_json("invalid_arguments", "paths must be non-empty");
    CheckpointRecord record;
    record.checkpoint_id = to_std(workspace::generate_uuid());
    record.session_id = ws_ctx_.session_id.empty() ? std::string("default") : to_std(ws_ctx_.session_id);
    record.description = description;
    record.created_at = now_iso();

    std::unordered_set<std::string> seen;
    for (const auto& input : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(input, normalized, path_error)) return error_json("path_outside_workspace", path_error);
        if (!seen.insert(normalized).second) continue;

        auto full_path = project_root() / normalized;
        CheckpointFileRecord file;
        file.path = normalized;
        std::error_code ec;
        file.existed = std::filesystem::exists(full_path, ec);
        if (file.existed) {
            std::string read_error;
            file.content = read_text(full_path, read_error);
            if (!read_error.empty()) return error_json("checkpoint_read_failed", read_error);
            file.hash = hash_content(file.content);
            file.size = file.content.size();
        }
        record.files.push_back(std::move(file));
    }

    std::string error;
    if (!save(record, error)) return error_json("checkpoint_save_failed", error);
    return Json{{"success", true}, {"checkpoint_id", record.checkpoint_id}, {"checkpoint", to_json(record)}};
}

Json CheckpointService::list() const {
    Json checkpoints = Json::array();
    auto dir = base_dir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return Json{{"success", true}, {"checkpoints", checkpoints}};
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return error_json("checkpoint_list_failed", ec.message());
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::string error;
        auto record = load(entry.path().stem().string(), error);
        if (!record) continue;
        checkpoints.push_back(Json{{"checkpoint_id", record->checkpoint_id},
                                   {"description", record->description},
                                   {"created_at", record->created_at},
                                   {"restored", record->restored},
                                   {"files", static_cast<int>(record->files.size())}});
    }
    return Json{{"success", true}, {"checkpoints", checkpoints}};
}

Json CheckpointService::read(std::string_view checkpoint_id) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return error_json("checkpoint_not_found", error);
    return Json{{"success", true}, {"checkpoint", to_json(*record)}};
}

Json CheckpointService::restore(std::string_view checkpoint_id, const std::vector<std::string>& paths, bool force) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return error_json("checkpoint_not_found", error);

    std::unordered_set<std::string> selected;
    for (const auto& input : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(input, normalized, path_error)) return error_json("path_outside_workspace", path_error);
        selected.insert(normalized);
    }

    Json restored = Json::array();
    for (const auto& file : record->files) {
        if (!selected.empty() && !selected.count(file.path)) continue;
        auto full_path = project_root() / file.path;
        std::error_code ec;
        bool exists_now = std::filesystem::exists(full_path, ec);
        if (!force && file.existed && exists_now) {
            std::string read_error;
            auto current = read_text(full_path, read_error);
            if (!read_error.empty()) return error_json("checkpoint_read_failed", read_error);
            if (hash_content(current) != file.hash) {
                return error_json("checkpoint_conflict", "file changed since checkpoint; pass force=true to restore");
            }
        }
        if (file.existed) {
            std::string write_error;
            if (!write_text(full_path, file.content, write_error)) return error_json("checkpoint_restore_failed", write_error);
        } else if (exists_now) {
            std::filesystem::remove(full_path, ec);
            if (ec) return error_json("checkpoint_restore_failed", ec.message());
        }
        restored.push_back(file.path);
    }

    record->restored = true;
    record->restored_at = now_iso();
    std::string save_error;
    if (!save(*record, save_error)) return error_json("checkpoint_save_failed", save_error);
    return Json{{"success", true}, {"checkpoint_id", record->checkpoint_id}, {"restored", restored}};
}

Json CheckpointService::remove(std::string_view checkpoint_id) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return error_json("checkpoint_not_found", error);
    std::error_code ec;
    std::filesystem::remove(checkpoint_path(checkpoint_id), ec);
    if (ec) return error_json("checkpoint_delete_failed", ec.message());
    return Json{{"success", true}, {"checkpoint_id", std::string(checkpoint_id)}};
}

} // namespace ben_gear::checkpoint
