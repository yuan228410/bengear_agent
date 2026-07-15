#include "intelligence/diagnostic_repair/diagnostic_repair_patch_draft_service.hpp"

#include "workspace/uuid.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ben_gear::diagnostic_repair {

namespace {

domain::AppError app_error(std::string_view code, std::string_view message) {
    return domain::AppError::invalid_argument(std::string(code), std::string(message));
}

int clamp_int(int value, int fallback, int min_value, int max_value) {
    if (value <= 0) value = fallback;
    return std::max(min_value, std::min(value, max_value));
}

std::filesystem::path project_root(const workspace::WorkspaceContext& ws_ctx) {
    if (!ws_ctx.project_path.empty()) return std::filesystem::path(ws_ctx.project_path.c_str());
    return ws_ctx.tier_paths.workspace_dir;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

std::string source_from_output(const RepairPatchDraftRequest& request) {
    if (!request.missing_source.empty()) return request.missing_source;
    auto output = request.preview_request.plan_request.context.output;
    auto marker = std::string("Cannot find source file:");
    auto pos = output.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    while (pos < output.size() && std::isspace(static_cast<unsigned char>(output[pos]))) ++pos;
    auto end = pos;
    while (end < output.size() && !std::isspace(static_cast<unsigned char>(output[end]))) ++end;
    return output.substr(pos, end - pos);
}

std::string make_insert_hunk_diff(const std::string& path, const std::vector<std::string>& old_lines, const std::string& inserted_line, int insert_index) {
    auto old_line_number = std::max(1, insert_index);
    auto context_before = std::max(1, insert_index - 2);
    auto context_after = std::min(static_cast<int>(old_lines.size()), insert_index + 1);
    std::ostringstream out;
    out << "diff --git a/" << path << " b/" << path << "\n";
    out << "--- a/" << path << "\n";
    out << "+++ b/" << path << "\n";
    auto old_count = context_after - context_before + 1;
    auto new_count = old_count + 1;
    out << "@@ -" << context_before << ',' << old_count << " +" << context_before << ',' << new_count << " @@\n";
    for (int line = context_before; line <= context_after; ++line) {
        if (line == old_line_number) out << '+' << inserted_line << '\n';
        out << ' ' << old_lines[static_cast<size_t>(line - 1)] << '\n';
    }
    return out.str();
}

Json no_draft_reason(std::string code, std::string message, Json details = Json::object()) {
    return Json{{"code", std::move(code)}, {"message", std::move(message)}, {"details", std::move(details)}};
}

bool line_starts_target(const std::string& line, const std::string& target) {
    if (line.find("add_library(") == std::string::npos && line.find("add_executable(") == std::string::npos) return false;
    if (target.empty()) return true;
    return line.find(target) != std::string::npos;
}

bool insert_source_into_cmake(std::vector<std::string>& lines, const std::string& source, const std::string& target, int& insert_index, std::string& inserted_line) {
    bool in_target = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!in_target && line_starts_target(lines[i], target)) {
            in_target = true;
            if (lines[i].find(source) != std::string::npos) return false;
            continue;
        }
        if (in_target) {
            if (lines[i].find(source) != std::string::npos) return false;
            if (lines[i].find(')') != std::string::npos) {
                auto indent = std::string("    ");
                inserted_line = indent + source;
                lines.insert(lines.begin() + static_cast<long>(i), inserted_line);
                insert_index = static_cast<int>(i) + 1;
                return true;
            }
        }
    }
    return false;
}

Json touched_file(std::string path) {
    return Json{{"path", std::move(path)}, {"change", "modify"}};
}

} // namespace

DiagnosticRepairPatchDraftService::DiagnosticRepairPatchDraftService(
    workspace::WorkspaceContext ws_ctx,
    std::shared_ptr<DiagnosticRepairPatchPreviewService> preview_service)
    : ws_ctx_(std::move(ws_ctx)), preview_service_(std::move(preview_service)) {
    if (!preview_service_) preview_service_ = std::make_shared<DiagnosticRepairPatchPreviewService>(ws_ctx_);
}

domain::AppResult<RepairPatchDraftRequest> repair_patch_draft_request_from_json(const Json& request) {
    if (!request.is_object()) {
        return domain::AppResult<RepairPatchDraftRequest>::failure(app_error("invalid_arguments", "request must be a JSON object"));
    }
    RepairPatchDraftRequest parsed;
    parsed.draft_hint = request.value("draft_hint", "");
    parsed.missing_source = request.value("missing_source", "");
    parsed.cmake_target = request.value("cmake_target", "");
    parsed.max_diff_bytes = clamp_int(request.value("max_diff_bytes", 200 * 1024), 200 * 1024, 1, 1024 * 1024);
    if (request.contains("code_context") && request["code_context"].is_object()) parsed.code_context = request["code_context"];
    auto preview_request = request;
    if (!preview_request.contains("unified_diff")) preview_request["unified_diff"] = "diff --git a/.bengear-draft-probe b/.bengear-draft-probe\n--- a/.bengear-draft-probe\n+++ b/.bengear-draft-probe\n@@ -0,0 +1 @@\n+probe\n";
    auto parsed_preview = repair_patch_preview_request_from_json(preview_request);
    if (!parsed_preview.ok()) return domain::AppResult<RepairPatchDraftRequest>::failure(parsed_preview.error());
    parsed.preview_request = std::move(parsed_preview.value());
    parsed.preview_request.unified_diff.clear();
    parsed.preview_request.plan_request.context.code_context = parsed.code_context;
    return domain::AppResult<RepairPatchDraftRequest>::success(std::move(parsed));
}

domain::AppResult<RepairPatchDraftResult> DiagnosticRepairPatchDraftService::repair_patch_draft(RepairPatchDraftRequest request) const {
    RepairPatchDraftResult result;
    result.draft_id = workspace::generate_uuid();
    result.draft_provider = "deterministic";
    result.plan_id = request.preview_request.plan_id;
    result.context_pack_id = request.code_context.value("context_pack_id", "");

    auto missing_source = source_from_output(request);
    if (missing_source.empty()) {
        result.no_draft_reasons.push_back(no_draft_reason("no_matching_rule", "no deterministic patch draft rule matched the available diagnostics"));
        result.validation_notes.push_back("provide a unified diff manually or add draft_hint/missing_source for deterministic drafting");
        return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
    }

    auto root = project_root(ws_ctx_);
    auto cmake_rel = std::string("CMakeLists.txt");
    auto cmake_path = root / cmake_rel;
    auto old_lines = read_lines(cmake_path);
    if (old_lines.empty()) {
        result.no_draft_reasons.push_back(no_draft_reason("cmake_missing", "CMakeLists.txt was not found or is empty", Json{{"path", cmake_rel}}));
        result.validation_notes.push_back("CMakeLists.txt was not found or is empty");
        return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
    }
    auto new_lines = old_lines;
    int insert_line = 0;
    std::string inserted_line;
    if (!insert_source_into_cmake(new_lines, missing_source, request.cmake_target, insert_line, inserted_line)) {
        result.no_draft_reasons.push_back(no_draft_reason("cmake_target_not_found", "could not safely identify a CMake target insertion point", Json{{"target", request.cmake_target}}));
        result.validation_notes.push_back("could not safely identify a CMake target insertion point");
        return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
    }

    auto diff = make_insert_hunk_diff(cmake_rel, old_lines, inserted_line, insert_line);
    if (diff.size() > static_cast<size_t>(request.max_diff_bytes)) {
        result.no_draft_reasons.push_back(no_draft_reason("diff_too_large", "generated diff exceeds max_diff_bytes", Json{{"max_diff_bytes", request.max_diff_bytes}}));
        result.validation_notes.push_back("generated diff exceeds max_diff_bytes");
        return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
    }

    request.preview_request.unified_diff = diff;
    auto preview = preview_service_->repair_patch_preview(request.preview_request);
    if (!preview.ok()) {
        result.status = "invalid";
        result.unified_diff = std::move(diff);
        result.validation_notes.push_back("generated draft failed patch preview validation");
        result.preview = Json{{"success", false}, {"error_type", preview.error().code}, {"message", preview.error().message}};
        return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
    }

    result.drafted = true;
    result.status = "previewed";
    result.draft_rule = "deterministic_cmake_missing_source";
    result.unified_diff = std::move(diff);
    result.touched_files.push_back(touched_file(cmake_rel));
    result.rationale.push_back(Json{{"kind", "cmake_missing_source"}, {"message", "added missing source file to a CMake target"}, {"source", missing_source}, {"line", insert_line}});
    result.confidence = request.cmake_target.empty() ? 65 : 78;
    result.risk_level = "medium";
    result.validation_notes.push_back("deterministic CMake source insertion draft; review target placement before apply");
    result.preview = to_json(preview.value());
    return domain::AppResult<RepairPatchDraftResult>::success(std::move(result));
}

Json to_json(const RepairPatchDraftResult& result) {
    return Json{{"success", true},
                {"provider", "diagnostic_repair_patch_draft"},
                {"read_only", true},
                {"drafted", result.drafted},
                {"status", result.status},
                {"draft_provider", result.draft_provider},
                {"draft_rule", result.draft_rule},
                {"draft_id", result.draft_id},
                {"plan_id", result.plan_id},
                {"context_pack_id", result.context_pack_id},
                {"unified_diff", result.unified_diff},
                {"touched_files", result.touched_files},
                {"rationale", result.rationale},
                {"confidence", result.confidence},
                {"risk_level", result.risk_level},
                {"validation_notes", result.validation_notes},
                {"no_draft_reasons", result.no_draft_reasons},
                {"preview", result.preview}};
}

} // namespace ben_gear::diagnostic_repair
