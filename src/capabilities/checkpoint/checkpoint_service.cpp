#include "capabilities/checkpoint/checkpoint_service.hpp"

#include "base/platform/os.hpp"
#include "workspace/uuid.hpp"

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

namespace container = base::container;

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = ben_gear::base::platform::compat::safe_gmtime(time);
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

domain::AppError app_error(domain::AppErrorCategory category, std::string_view code, std::string_view message) {
    container::String error_code(code.data(), code.size());
    container::String error_message(message.data(), message.size());
    switch (category) {
    case domain::AppErrorCategory::invalid_argument:
        return domain::AppError::invalid_argument(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::not_found:
        return domain::AppError::not_found(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::permission_denied:
        return domain::AppError::permission_denied(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::conflict:
        return domain::AppError::conflict(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::unavailable:
        return domain::AppError::unavailable(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::internal:
        return domain::AppError::internal(std::move(error_code), std::move(error_message));
    }
    return domain::AppError::internal(std::move(error_code), std::move(error_message));
}

domain::AppError invalid_argument(std::string_view code, std::string_view message) {
    return app_error(domain::AppErrorCategory::invalid_argument, code, message);
}

domain::AppError not_found(std::string_view code, std::string_view message) {
    return app_error(domain::AppErrorCategory::not_found, code, message);
}

domain::AppError conflict(std::string_view code, std::string_view message) {
    return app_error(domain::AppErrorCategory::conflict, code, message);
}

domain::AppError unavailable(std::string_view code, std::string_view message) {
    return app_error(domain::AppErrorCategory::unavailable, code, message);
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

Json to_json(const CheckpointCreateResult& result) {
    return Json{{"success", true}, {"checkpoint_id", result.checkpoint_id}, {"checkpoint", to_json(result.checkpoint)}};
}

Json to_json(const CheckpointListEntry& entry) {
    return Json{{"checkpoint_id", entry.checkpoint_id},
                {"description", entry.description},
                {"created_at", entry.created_at},
                {"restored", entry.restored},
                {"files", entry.files}};
}

Json to_json(const CheckpointListResult& result) {
    Json checkpoints = Json::array();
    for (const auto& checkpoint : result.checkpoints) checkpoints.push_back(to_json(checkpoint));
    return Json{{"success", true}, {"checkpoints", checkpoints}};
}

Json to_json(const CheckpointReadResult& result) {
    return Json{{"success", true}, {"checkpoint", to_json(result.checkpoint)}};
}

Json to_json(const CheckpointRestoreResult& result) {
    Json restored = Json::array();
    for (const auto& path : result.restored) restored.push_back(path);
    return Json{{"success", true}, {"checkpoint_id", result.checkpoint_id}, {"restored", restored}};
}

Json to_json(const CheckpointRemoveResult& result) {
    return Json{{"success", true}, {"checkpoint_id", result.checkpoint_id}};
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

domain::AppResult<CheckpointCreateResult> CheckpointService::create(const std::vector<std::string>& paths, const std::string& description) const {
    if (paths.empty()) return domain::AppResult<CheckpointCreateResult>::failure(invalid_argument("invalid_arguments", "paths must be non-empty"));
    CheckpointRecord record;
    record.checkpoint_id = to_std(workspace::generate_uuid());
    record.session_id = ws_ctx_.session_id.empty() ? std::string("default") : to_std(ws_ctx_.session_id);
    record.description = description;
    record.created_at = now_iso();

    std::unordered_set<std::string> seen;
    for (const auto& input : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(input, normalized, path_error)) return domain::AppResult<CheckpointCreateResult>::failure(invalid_argument("path_outside_workspace", path_error));
        if (!seen.insert(normalized).second) continue;

        auto full_path = project_root() / normalized;
        CheckpointFileRecord file;
        file.path = normalized;
        std::error_code ec;
        file.existed = std::filesystem::exists(full_path, ec);
        if (file.existed) {
            std::string read_error;
            file.content = read_text(full_path, read_error);
            if (!read_error.empty()) return domain::AppResult<CheckpointCreateResult>::failure(unavailable("checkpoint_read_failed", read_error));
            file.hash = hash_content(file.content);
            file.size = file.content.size();
        }
        record.files.push_back(std::move(file));
    }

    std::string error;
    if (!save(record, error)) return domain::AppResult<CheckpointCreateResult>::failure(unavailable("checkpoint_save_failed", error));
    auto id = record.checkpoint_id;
    return domain::AppResult<CheckpointCreateResult>::success(CheckpointCreateResult{id, std::move(record)});
}

domain::AppResult<CheckpointListResult> CheckpointService::list() const {
    CheckpointListResult result;
    auto dir = base_dir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return domain::AppResult<CheckpointListResult>::success(std::move(result));
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return domain::AppResult<CheckpointListResult>::failure(unavailable("checkpoint_list_failed", ec.message()));
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::string error;
        auto record = load(entry.path().stem().string(), error);
        if (!record) continue;
        result.checkpoints.push_back(CheckpointListEntry{record->checkpoint_id,
                                                         record->description,
                                                         record->created_at,
                                                         record->restored,
                                                         static_cast<int>(record->files.size())});
    }
    return domain::AppResult<CheckpointListResult>::success(std::move(result));
}

domain::AppResult<CheckpointReadResult> CheckpointService::read(std::string_view checkpoint_id) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return domain::AppResult<CheckpointReadResult>::failure(not_found("checkpoint_not_found", error));
    return domain::AppResult<CheckpointReadResult>::success(CheckpointReadResult{std::move(*record)});
}

domain::AppResult<CheckpointRestoreResult> CheckpointService::restore(std::string_view checkpoint_id, const std::vector<std::string>& paths, bool force) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return domain::AppResult<CheckpointRestoreResult>::failure(not_found("checkpoint_not_found", error));

    std::unordered_set<std::string> selected;
    for (const auto& input : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(input, normalized, path_error)) return domain::AppResult<CheckpointRestoreResult>::failure(invalid_argument("path_outside_workspace", path_error));
        selected.insert(normalized);
    }

    std::vector<std::string> restored;
    for (const auto& file : record->files) {
        if (!selected.empty() && !selected.count(file.path)) continue;
        auto full_path = project_root() / file.path;
        std::error_code ec;
        bool exists_now = std::filesystem::exists(full_path, ec);
        if (!force && file.existed && exists_now) {
            std::string read_error;
            auto current = read_text(full_path, read_error);
            if (!read_error.empty()) return domain::AppResult<CheckpointRestoreResult>::failure(unavailable("checkpoint_read_failed", read_error));
            if (hash_content(current) != file.hash) {
                return domain::AppResult<CheckpointRestoreResult>::failure(conflict("checkpoint_conflict", "file changed since checkpoint; pass force=true to restore"));
            }
        }
        if (file.existed) {
            std::string write_error;
            if (!write_text(full_path, file.content, write_error)) return domain::AppResult<CheckpointRestoreResult>::failure(unavailable("checkpoint_restore_failed", write_error));
        } else if (exists_now) {
            std::filesystem::remove(full_path, ec);
            if (ec) return domain::AppResult<CheckpointRestoreResult>::failure(unavailable("checkpoint_restore_failed", ec.message()));
        }
        restored.push_back(file.path);
    }

    record->restored = true;
    record->restored_at = now_iso();
    std::string save_error;
    if (!save(*record, save_error)) return domain::AppResult<CheckpointRestoreResult>::failure(unavailable("checkpoint_save_failed", save_error));
    return domain::AppResult<CheckpointRestoreResult>::success(CheckpointRestoreResult{record->checkpoint_id, std::move(restored)});
}

domain::AppResult<CheckpointRemoveResult> CheckpointService::remove(std::string_view checkpoint_id) const {
    std::string error;
    auto record = load(checkpoint_id, error);
    if (!record) return domain::AppResult<CheckpointRemoveResult>::failure(not_found("checkpoint_not_found", error));
    std::error_code ec;
    std::filesystem::remove(checkpoint_path(checkpoint_id), ec);
    if (ec) return domain::AppResult<CheckpointRemoveResult>::failure(unavailable("checkpoint_delete_failed", ec.message()));
    return domain::AppResult<CheckpointRemoveResult>::success(CheckpointRemoveResult{std::string(checkpoint_id)});
}

} // namespace ben_gear::checkpoint
