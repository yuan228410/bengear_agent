#include "ben_gear/patch/change_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ben_gear::patch {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

FileChangeKind file_kind_from_string(const std::string& value) {
    if (value == "add") return FileChangeKind::add;
    if (value == "remove") return FileChangeKind::remove;
    return FileChangeKind::modify;
}

DiffLineKind line_kind_from_string(const std::string& value) {
    if (value == "add") return DiffLineKind::add;
    if (value == "remove") return DiffLineKind::remove;
    return DiffLineKind::context;
}

DiffLine diff_line_from_json(const Json& json) {
    DiffLine line;
    line.kind = line_kind_from_string(json.value("kind", "context"));
    line.text = json.value("text", "");
    return line;
}

DiffHunk diff_hunk_from_json(const Json& json) {
    DiffHunk hunk;
    hunk.old_start = json.value("old_start", 0);
    hunk.old_count = json.value("old_count", 0);
    hunk.new_start = json.value("new_start", 0);
    hunk.new_count = json.value("new_count", 0);
    auto lines = json["lines"];
    if (lines.is_array()) {
        for (size_t i = 0; i < lines.size(); ++i) hunk.lines.push_back(diff_line_from_json(lines[i]));
    }
    return hunk;
}

FilePatch file_patch_from_json(const Json& json) {
    FilePatch file;
    file.kind = file_kind_from_string(json.value("kind", "modify"));
    file.old_path = std::filesystem::path(std::string(json.value("old_path", "")));
    file.new_path = std::filesystem::path(std::string(json.value("new_path", "")));
    file.additions = json.value("additions", 0);
    file.deletions = json.value("deletions", 0);
    auto hunks = json["hunks"];
    if (hunks.is_array()) {
        for (size_t i = 0; i < hunks.size(); ++i) file.hunks.push_back(diff_hunk_from_json(hunks[i]));
    }
    return file;
}

PatchPreview patch_preview_from_json(const Json& json) {
    PatchPreview preview;
    if (!json.is_object()) return preview;
    preview.success = json.value("success", false);
    preview.error_type = json.value("error_type", "");
    preview.message = json.value("message", "");
    preview.can_apply = json.value("can_apply", false);
    auto files = json["files"];
    if (files.is_array()) {
        for (size_t i = 0; i < files.size(); ++i) {
            auto file = file_patch_from_json(files[i]);
            preview.additions += file.additions;
            preview.deletions += file.deletions;
            preview.files.push_back(std::move(file));
        }
    }
    auto summary = json["summary"];
    if (summary.is_object()) {
        preview.additions = summary.value("additions", preview.additions);
        preview.deletions = summary.value("deletions", preview.deletions);
    }
    return preview;
}

ChangedFileRecord changed_file_from_json(const Json& json) {
    ChangedFileRecord file;
    file.path = json.value("path", "");
    file.kind = json.value("kind", "modify");
    file.existed_before = json.value("existed_before", false);
    file.exists_after = json.value("exists_after", false);
    file.before_hash = json.value("before_hash", "");
    file.after_hash = json.value("after_hash", "");
    file.before_content = json.value("before_content", "");
    return file;
}

ChangeRecord change_record_from_json(const Json& json) {
    ChangeRecord record;
    record.change_id = json.value("change_id", "");
    record.session_id = json.value("session_id", "");
    record.description = json.value("description", "");
    record.created_at = json.value("created_at", "");
    record.reverted = json.value("reverted", false);
    record.reverted_at = json.value("reverted_at", "");
    auto files = json["files"];
    if (files.is_array()) {
        for (size_t i = 0; i < files.size(); ++i) {
            record.files.push_back(changed_file_from_json(files[i]));
        }
    }
    if (json.contains("patch")) record.patch = patch_preview_from_json(json["patch"]);
    return record;
}

} // namespace

ChangeStore::ChangeStore(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

std::filesystem::path ChangeStore::base_dir() const {
    return ws_ctx_.tier_paths.user_dir / "changes" / to_std(ws_ctx_.session_id);
}

std::filesystem::path ChangeStore::change_path(std::string_view change_id) const {
    auto id = std::string(change_id.data(), change_id.size());
    return base_dir() / (id + ".json");
}

bool ChangeStore::save(const ChangeRecord& record, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(base_dir(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    auto path = change_path(record.change_id);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "failed to open change record for writing";
            return false;
        }
        auto text = to_json(record).dump(2);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            error = "failed to write change record";
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

std::optional<ChangeRecord> ChangeStore::load(std::string_view change_id, std::string& error) const {
    std::ifstream in(change_path(change_id), std::ios::binary);
    if (!in) {
        error = "change record not found";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string parse_error;
    auto json = parse_json(buffer.str(), parse_error);
    if (!parse_error.empty() || !json.is_object()) {
        error = parse_error.empty() ? "invalid change record" : parse_error;
        return std::nullopt;
    }
    return change_record_from_json(json);
}

std::vector<ChangeRecord> ChangeStore::list(std::string& error) const {
    std::vector<ChangeRecord> records;
    auto dir = base_dir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return records;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            error = ec.message();
            return records;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto id = entry.path().stem().string();
        std::string load_error;
        auto record = load(id, load_error);
        if (record) records.push_back(std::move(*record));
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.created_at > b.created_at;
    });
    return records;
}

} // namespace ben_gear::patch
