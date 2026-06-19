#include "ben_gear/patch/patch_service.hpp"
#include "ben_gear/patch/diff_parser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace ben_gear::patch {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

std::string now_id() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "chg_" + std::to_string(ms);
}

std::string now_text() {
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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_file_atomic(const std::filesystem::path& path, std::string_view content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "failed to open temp file";
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) {
            error = "failed to write temp file";
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = ec.message();
        return false;
    }
    return true;
}

std::string hash_content(std::string_view content) {
    auto value = std::hash<std::string_view>{}(content);
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < text.size()) lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out.push_back('\n');
    }
    return out;
}

bool apply_file_patch(std::vector<std::string>& lines, const FilePatch& patch, std::string& error) {
    int offset = 0;
    for (const auto& hunk : patch.hunks) {
        int pos = hunk.old_start <= 0 ? 0 : hunk.old_start - 1 + offset;
        if (pos < 0 || pos > static_cast<int>(lines.size())) {
            error = "hunk position out of range";
            return false;
        }
        std::vector<std::string> replacement;
        int cursor = pos;
        for (const auto& line : hunk.lines) {
            if (line.kind == DiffLineKind::context || line.kind == DiffLineKind::remove) {
                if (cursor >= static_cast<int>(lines.size()) || lines[static_cast<size_t>(cursor)] != line.text) {
                    error = "patch context does not match";
                    return false;
                }
                ++cursor;
            }
            if (line.kind == DiffLineKind::context || line.kind == DiffLineKind::add) {
                replacement.push_back(line.text);
            }
        }
        lines.erase(lines.begin() + pos, lines.begin() + cursor);
        lines.insert(lines.begin() + pos, replacement.begin(), replacement.end());
        offset += static_cast<int>(replacement.size()) - (cursor - pos);
    }
    return true;
}

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}};
}

} // namespace

std::string to_string(FileChangeKind kind) {
    switch (kind) {
        case FileChangeKind::add: return "add";
        case FileChangeKind::modify: return "modify";
        case FileChangeKind::remove: return "remove";
    }
    return "modify";
}

std::string to_string(DiffLineKind kind) {
    switch (kind) {
        case DiffLineKind::context: return "context";
        case DiffLineKind::add: return "add";
        case DiffLineKind::remove: return "remove";
    }
    return "context";
}

Json to_json(const DiffLine& line) {
    return Json{{"kind", to_string(line.kind)}, {"text", line.text}};
}

Json to_json(const DiffHunk& hunk) {
    Json lines = Json::array();
    for (const auto& line : hunk.lines) lines.push_back(to_json(line));
    return Json{{"old_start", hunk.old_start}, {"old_count", hunk.old_count}, {"new_start", hunk.new_start}, {"new_count", hunk.new_count}, {"lines", lines}};
}

Json to_json(const FilePatch& file) {
    Json hunks = Json::array();
    for (const auto& hunk : file.hunks) hunks.push_back(to_json(hunk));
    return Json{{"kind", to_string(file.kind)}, {"old_path", file.old_path.string()}, {"new_path", file.new_path.string()}, {"additions", file.additions}, {"deletions", file.deletions}, {"hunks", hunks}};
}

Json to_json(const PatchPreview& preview) {
    Json files = Json::array();
    for (const auto& file : preview.files) files.push_back(to_json(file));
    return Json{{"success", preview.success}, {"error_type", preview.error_type}, {"message", preview.message}, {"can_apply", preview.can_apply}, {"files", files}, {"summary", Json{{"files_changed", static_cast<int>(preview.files.size())}, {"additions", preview.additions}, {"deletions", preview.deletions}}}};
}

Json to_json(const ChangedFileRecord& file) {
    return Json{{"path", file.path}, {"kind", file.kind}, {"existed_before", file.existed_before}, {"exists_after", file.exists_after}, {"before_hash", file.before_hash}, {"after_hash", file.after_hash}, {"before_content", file.before_content}};
}

Json to_json(const ChangeRecord& record) {
    Json files = Json::array();
    for (const auto& file : record.files) files.push_back(to_json(file));
    return Json{{"change_id", record.change_id}, {"session_id", record.session_id}, {"description", record.description}, {"created_at", record.created_at}, {"files", files}, {"reverted", record.reverted}, {"reverted_at", record.reverted_at}, {"patch", to_json(record.patch)}};
}

PatchService::PatchService(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(ws_ctx), store_(ws_ctx) {}

std::filesystem::path PatchService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

std::filesystem::path PatchService::resolve_workspace_path(const std::filesystem::path& relative, std::string& error) const {
    if (relative.empty() || relative.is_absolute()) {
        error = "patch paths must be relative to the workspace";
        return {};
    }
    auto generic = relative.generic_string();
    if (generic == ".." || generic.rfind("../", 0) == 0 || generic.find("/../") != std::string::npos) {
        error = "patch path escapes workspace";
        return {};
    }
    for (const auto& part : relative) {
        if (part.string() == "..") {
            error = "patch path escapes workspace";
            return {};
        }
    }
    auto root = std::filesystem::weakly_canonical(project_root());
    auto target = std::filesystem::weakly_canonical(root / relative);
    auto root_text = root.string();
    auto target_text = target.string();
    if (target_text != root_text && target_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) != 0) {
        error = "patch path escapes workspace";
        return {};
    }
    return target;
}

std::string PatchService::relative_display_path(const std::filesystem::path& path) const {
    std::error_code ec;
    auto rel = std::filesystem::relative(path, project_root(), ec);
    return ec ? path.string() : rel.string();
}

PatchPreview PatchService::preview(std::string_view unified_diff) const {
    return parse_unified_diff(unified_diff);
}

Json PatchService::apply(std::string_view unified_diff, std::string_view description) {
    auto parsed = preview(unified_diff);
    if (!parsed.success) return to_json(parsed);

    struct PendingWrite {
        std::filesystem::path path;
        FilePatch patch;
        bool existed_before = false;
        std::string before_content;
        std::string after_content;
    };
    std::vector<PendingWrite> pending;

    for (const auto& file : parsed.files) {
        std::string path_error;
        auto rel_path = file.kind == FileChangeKind::remove ? file.old_path : file.new_path;
        auto target = resolve_workspace_path(rel_path, path_error);
        if (!path_error.empty()) return error_json("path_outside_workspace", path_error);

        PendingWrite write;
        write.path = target;
        write.patch = file;
        write.existed_before = std::filesystem::exists(target);
        write.before_content = write.existed_before ? read_file(target) : std::string();

        if (file.kind != FileChangeKind::add && !write.existed_before) {
            return error_json("patch_conflict", "target file does not exist");
        }
        auto lines = split_lines(write.before_content);
        std::string patch_error;
        if (!apply_file_patch(lines, file, patch_error)) {
            return error_json("patch_conflict", patch_error);
        }
        write.after_content = join_lines(lines);
        pending.push_back(std::move(write));
    }

    ChangeRecord record;
    record.change_id = now_id();
    record.session_id = to_std(ws_ctx_.session_id);
    record.description = std::string(description.data(), description.size());
    record.created_at = now_text();
    record.patch = parsed;

    for (const auto& write : pending) {
        std::string error;
        if (write.patch.kind == FileChangeKind::remove) {
            std::error_code ec;
            std::filesystem::remove(write.path, ec);
            if (ec) return error_json("write_failed", ec.message());
        } else if (!write_file_atomic(write.path, write.after_content, error)) {
            return error_json("write_failed", error);
        }

        ChangedFileRecord file;
        file.path = relative_display_path(write.path);
        file.kind = to_string(write.patch.kind);
        file.existed_before = write.existed_before;
        file.exists_after = write.patch.kind != FileChangeKind::remove;
        file.before_content = write.before_content;
        file.before_hash = hash_content(write.before_content);
        file.after_hash = file.exists_after ? hash_content(write.after_content) : std::string();
        record.files.push_back(std::move(file));
    }

    std::string store_error;
    if (!store_.save(record, store_error)) return error_json("change_store_failed", store_error);

    Json files = Json::array();
    for (const auto& file : record.files) files.push_back(to_json(file));
    return Json{{"success", true}, {"change_id", record.change_id}, {"files", files}, {"summary", Json{{"files_changed", static_cast<int>(record.files.size())}, {"additions", parsed.additions}, {"deletions", parsed.deletions}}}};
}

Json PatchService::list_changes() const {
    std::string error;
    auto records = store_.list(error);
    if (!error.empty()) return error_json("change_store_failed", error);
    Json changes = Json::array();
    for (const auto& record : records) {
        changes.push_back(Json{{"change_id", record.change_id},
                               {"description", record.description},
                               {"created_at", record.created_at},
                               {"reverted", record.reverted},
                               {"files_changed", static_cast<int>(record.files.size())}});
    }
    return Json{{"success", true}, {"changes", changes}};
}

Json PatchService::read_change(std::string_view change_id) const {
    std::string error;
    auto record = store_.load(change_id, error);
    if (!record) return error_json("change_not_found", error);
    return Json{{"success", true}, {"change", to_json(*record)}};
}

Json PatchService::revert(std::string_view change_id, bool force) {
    std::string load_error;
    auto record_opt = store_.load(change_id, load_error);
    if (!record_opt) return error_json("change_not_found", load_error);
    auto record = *record_opt;
    if (record.reverted) return error_json("already_reverted", "change has already been reverted");

    for (const auto& file : record.files) {
        std::string path_error;
        auto path = resolve_workspace_path(file.path, path_error);
        if (!path_error.empty()) return error_json("path_outside_workspace", path_error);
        auto current = std::filesystem::exists(path) ? read_file(path) : std::string();
        auto current_hash = std::filesystem::exists(path) ? hash_content(current) : std::string();
        if (!force && current_hash != file.after_hash) {
            return error_json("revert_conflict", "file changed after patch application: " + file.path);
        }
    }

    Json reverted = Json::array();
    for (const auto& file : record.files) {
        std::string path_error;
        auto path = resolve_workspace_path(file.path, path_error);
        if (!file.existed_before) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) return error_json("write_failed", ec.message());
        } else {
            std::string error;
            if (!write_file_atomic(path, file.before_content, error)) return error_json("write_failed", error);
        }
        reverted.push_back(file.path);
    }

    record.reverted = true;
    record.reverted_at = now_text();
    std::string save_error;
    store_.save(record, save_error);
    return Json{{"success", true}, {"change_id", std::string(change_id)}, {"reverted_files", reverted}};
}

} // namespace ben_gear::patch
