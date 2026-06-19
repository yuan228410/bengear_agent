#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ben_gear::patch {

enum class FileChangeKind {
    add,
    modify,
    remove,
};

enum class DiffLineKind {
    context,
    add,
    remove,
};

struct DiffLine {
    DiffLineKind kind = DiffLineKind::context;
    std::string text;
};

struct DiffHunk {
    int old_start = 0;
    int old_count = 0;
    int new_start = 0;
    int new_count = 0;
    std::vector<DiffLine> lines;
};

struct FilePatch {
    FileChangeKind kind = FileChangeKind::modify;
    std::filesystem::path old_path;
    std::filesystem::path new_path;
    std::vector<DiffHunk> hunks;
    int additions = 0;
    int deletions = 0;
};

struct PatchPreview {
    bool success = false;
    std::string error_type;
    std::string message;
    bool can_apply = false;
    std::vector<FilePatch> files;
    int additions = 0;
    int deletions = 0;
};

struct ChangedFileRecord {
    std::string path;
    std::string kind;
    bool existed_before = false;
    bool exists_after = false;
    std::string before_hash;
    std::string after_hash;
    std::string before_content;
};

struct ChangeRecord {
    std::string change_id;
    std::string session_id;
    std::string description;
    std::string created_at;
    std::vector<ChangedFileRecord> files;
    bool reverted = false;
    std::string reverted_at;
    PatchPreview patch;
};

std::string to_string(FileChangeKind kind);
std::string to_string(DiffLineKind kind);
Json to_json(const DiffLine& line);
Json to_json(const DiffHunk& hunk);
Json to_json(const FilePatch& file);
Json to_json(const PatchPreview& preview);
Json to_json(const ChangedFileRecord& file);
Json to_json(const ChangeRecord& record);

} // namespace ben_gear::patch
