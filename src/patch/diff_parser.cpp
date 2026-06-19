#include "ben_gear/patch/diff_parser.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace ben_gear::patch {

namespace {

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

std::string strip_prefix(std::string path) {
    if (path.rfind("a/", 0) == 0 || path.rfind("b/", 0) == 0) return path.substr(2);
    return path;
}

std::string parse_diff_path(std::string_view line, std::string_view marker) {
    if (line.rfind(marker, 0) != 0) return {};
    std::string path(line.substr(marker.size()));
    auto tab = path.find('\t');
    if (tab != std::string::npos) path.resize(tab);
    if (path == "/dev/null") return {};
    return strip_prefix(path);
}

bool parse_hunk_header(const std::string& line, DiffHunk& hunk) {
    int old_start = 0, old_count = 1, new_start = 0, new_count = 1;
    auto parsed = std::sscanf(line.c_str(), "@@ -%d,%d +%d,%d @@", &old_start, &old_count, &new_start, &new_count);
    if (parsed < 4) {
        old_count = 1;
        new_count = 1;
        parsed = std::sscanf(line.c_str(), "@@ -%d,%d +%d @@", &old_start, &old_count, &new_start);
        if (parsed < 3) {
            old_count = 1;
            new_count = 1;
            parsed = std::sscanf(line.c_str(), "@@ -%d +%d,%d @@", &old_start, &new_start, &new_count);
        }
        if (parsed < 3) {
            old_count = 1;
            new_count = 1;
            parsed = std::sscanf(line.c_str(), "@@ -%d +%d @@", &old_start, &new_start);
            if (parsed < 2) return false;
        }
    }
    hunk.old_start = old_start;
    hunk.old_count = old_count;
    hunk.new_start = new_start;
    hunk.new_count = new_count;
    return true;
}

} // namespace

PatchPreview empty_patch_preview() {
    PatchPreview preview;
    preview.success = true;
    preview.can_apply = false;
    return preview;
}

PatchPreview parse_unified_diff(std::string_view unified_diff) {
    PatchPreview preview;
    auto lines = split_lines(unified_diff);
    FilePatch* current = nullptr;
    DiffHunk* current_hunk = nullptr;
    std::string old_path;
    std::string new_path;

    for (const auto& line : lines) {
        if (line.rfind("Binary files ", 0) == 0 || line.rfind("GIT binary patch", 0) == 0) {
            preview.error_type = "binary_file_rejected";
            preview.message = "binary patches are not supported";
            return preview;
        }
        if (line.rfind("diff --git ", 0) == 0) {
            current = nullptr;
            current_hunk = nullptr;
            old_path.clear();
            new_path.clear();
            continue;
        }
        if (line.rfind("--- ", 0) == 0) {
            old_path = parse_diff_path(line, "--- ");
            continue;
        }
        if (line.rfind("+++ ", 0) == 0) {
            new_path = parse_diff_path(line, "+++ ");
            FilePatch file;
            file.old_path = old_path;
            file.new_path = new_path;
            if (old_path.empty() && !new_path.empty()) file.kind = FileChangeKind::add;
            else if (!old_path.empty() && new_path.empty()) file.kind = FileChangeKind::remove;
            else file.kind = FileChangeKind::modify;
            preview.files.push_back(std::move(file));
            current = &preview.files.back();
            current_hunk = nullptr;
            continue;
        }
        if (line.rfind("@@ ", 0) == 0) {
            if (!current) {
                preview.error_type = "invalid_patch";
                preview.message = "hunk appears before file header";
                return preview;
            }
            DiffHunk hunk;
            if (!parse_hunk_header(line, hunk)) {
                preview.error_type = "invalid_patch";
                preview.message = "invalid hunk header";
                return preview;
            }
            current->hunks.push_back(std::move(hunk));
            current_hunk = &current->hunks.back();
            continue;
        }
        if (!current_hunk) continue;
        if (line == R"(\ No newline at end of file)") continue;
        if (line.empty()) {
            current_hunk->lines.push_back({DiffLineKind::context, ""});
            continue;
        }
        char prefix = line[0];
        std::string text = line.size() > 1 ? line.substr(1) : std::string();
        if (prefix == ' ') current_hunk->lines.push_back({DiffLineKind::context, std::move(text)});
        else if (prefix == '+') {
            current_hunk->lines.push_back({DiffLineKind::add, std::move(text)});
            ++current->additions;
            ++preview.additions;
        } else if (prefix == '-') {
            current_hunk->lines.push_back({DiffLineKind::remove, std::move(text)});
            ++current->deletions;
            ++preview.deletions;
        }
    }

    if (preview.files.empty()) {
        preview.error_type = "invalid_patch";
        preview.message = "no file patches found";
        return preview;
    }
    preview.success = true;
    preview.can_apply = true;
    return preview;
}

} // namespace ben_gear::patch
