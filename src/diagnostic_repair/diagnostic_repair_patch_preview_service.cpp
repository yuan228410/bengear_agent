#include "ben_gear/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::diagnostic_repair {

namespace {

constexpr int kDefaultMaxDiffBytes = 200 * 1024;
constexpr int kMaxDiffBytesCeiling = 1024 * 1024;

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false},
                {"error_type", std::string(type)},
                {"message", std::string(message)},
                {"provider", "diagnostic_repair_patch_preview"},
                {"read_only", true}};
}

int clamp_int(int value, int min_value, int max_value) {
    return std::clamp(value, min_value, max_value);
}

Json safety_json() {
    return Json{{"read_only", true},
                {"requires_user_approval_before_edit", true},
                {"writes_files", false},
                {"runs_commands", false},
                {"creates_checkpoints", false},
                {"applies_patch", false}};
}

std::string selected_plan_id(const Json& request, const Json& plan_result) {
    auto requested = request.value("plan_id", "");
    if (!requested.empty()) return requested;
    if (plan_result.contains("plans") && plan_result["plans"].is_array() && !plan_result["plans"].empty()) {
        return plan_result["plans"][0].value("id", "");
    }
    return {};
}

Json find_selected_plan(const Json& plan_result, const std::string& plan_id) {
    if (!plan_result.contains("plans") || !plan_result["plans"].is_array()) return Json::object();
    if (plan_id.empty()) return plan_result["plans"].empty() ? Json::object() : plan_result["plans"][0];
    for (const auto& plan : plan_result["plans"]) {
        if (plan.is_object() && plan.value("id", "") == plan_id) return plan;
    }
    return Json::object();
}

void collect_candidate_files_from_plan(const Json& selected_plan, std::set<std::string>& files) {
    if (!selected_plan.is_object() || !selected_plan.contains("candidate_files") || !selected_plan["candidate_files"].is_array()) return;
    for (const auto& file : selected_plan["candidate_files"]) {
        if (!file.is_object()) continue;
        auto path = file.value("path", "");
        if (!path.empty()) files.insert(std::move(path));
    }
}

void collect_touched_files_from_patch(const Json& patch_preview, std::set<std::string>& files) {
    if (!patch_preview.contains("files") || !patch_preview["files"].is_array()) return;
    for (const auto& file : patch_preview["files"]) {
        if (!file.is_object()) continue;
        auto path = file.value("new_path", "");
        if (path.empty() || path == "/dev/null") path = file.value("old_path", "");
        if (!path.empty() && path != "/dev/null") files.insert(std::move(path));
    }
}

Json array_from_set(const std::set<std::string>& values) {
    Json out = Json::array();
    for (const auto& value : values) out.push_back(value);
    return out;
}

Json candidate_match_json(const Json& selected_plan, const Json& patch_preview, Json& notes) {
    std::set<std::string> candidates;
    std::set<std::string> touched;
    collect_candidate_files_from_plan(selected_plan, candidates);
    collect_touched_files_from_patch(patch_preview, touched);

    bool matched = false;
    for (const auto& path : touched) {
        if (candidates.count(path)) {
            matched = true;
            break;
        }
    }
    if (!touched.empty() && !candidates.empty() && !matched) {
        notes.push_back("candidate diff does not touch the selected repair plan candidate files");
    }
    if (!selected_plan.is_object() || selected_plan.empty()) {
        notes.push_back("selected repair plan was not found; patch preview was validated independently");
    }

    return Json{{"matched", matched},
                {"touched_files", array_from_set(touched)},
                {"candidate_files", array_from_set(candidates)}};
}

} // namespace

DiagnosticRepairPatchPreviewService::DiagnosticRepairPatchPreviewService(
    workspace::WorkspaceContext ws_ctx,
    std::shared_ptr<DiagnosticRepairPlanService> plan_service,
    std::shared_ptr<patch::PatchService> patch_service)
    : ws_ctx_(std::move(ws_ctx)),
      plan_service_(std::move(plan_service)),
      patch_service_(std::move(patch_service)) {
    if (!plan_service_) plan_service_ = std::make_shared<DiagnosticRepairPlanService>(ws_ctx_);
    if (!patch_service_) patch_service_ = std::make_shared<patch::PatchService>(ws_ctx_);
}

Json DiagnosticRepairPatchPreviewService::repair_patch_preview(const Json& request) const {
    if (!request.is_object()) return error_json("invalid_arguments", "request must be a JSON object");
    if (!plan_service_) return error_json("service_unavailable", "diagnostic repair plan service unavailable");
    if (!patch_service_) return error_json("service_unavailable", "patch service unavailable");

    auto unified_diff = request.value("unified_diff", "");
    if (unified_diff.empty()) return error_json("invalid_arguments", "unified_diff is required");
    auto max_diff_bytes = clamp_int(request.value("max_diff_bytes", kDefaultMaxDiffBytes), 1, kMaxDiffBytesCeiling);
    if (unified_diff.size() > static_cast<size_t>(max_diff_bytes)) {
        return error_json("invalid_arguments", "unified_diff exceeds max_diff_bytes");
    }

    auto plan_request = request;
    plan_request.erase("unified_diff");
    plan_request.erase("plan_id");
    plan_request.erase("max_diff_bytes");
    auto plan_result = plan_service_->repair_plan(plan_request);
    if (!plan_result.value("success", false)) {
        plan_result["provider"] = "diagnostic_repair_patch_preview";
        plan_result["read_only"] = true;
        return plan_result;
    }

    auto patch_preview = patch_service_->preview_validated(unified_diff);
    auto plan_id = selected_plan_id(request, plan_result);
    auto selected_plan = find_selected_plan(plan_result, plan_id);
    Json notes = Json::array();
    auto candidate_match = candidate_match_json(selected_plan, patch_preview, notes);

    return Json{{"success", true},
                {"provider", "diagnostic_repair_patch_preview"},
                {"read_only", true},
                {"diagnostic_count", plan_result.value("diagnostic_count", 0)},
                {"plan_count", plan_result.value("plan_count", 0)},
                {"selected_plan_id", plan_id},
                {"repair_plan", plan_result},
                {"patch_preview", patch_preview},
                {"candidate_file_match", candidate_match},
                {"safety", safety_json()},
                {"notes", notes}};
}

} // namespace ben_gear::diagnostic_repair
