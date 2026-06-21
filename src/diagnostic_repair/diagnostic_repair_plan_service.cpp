#include "ben_gear/diagnostic_repair/diagnostic_repair_plan_service.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::diagnostic_repair {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_any(std::string_view text, std::initializer_list<std::string_view> needles) {
    for (auto needle : needles) {
        if (text.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

int clamp_int(int value, int min_value, int max_value) {
    return std::clamp(value, min_value, max_value);
}

domain::AppError app_error(std::string_view type, std::string_view message) {
    auto error = domain::AppError::invalid_argument(
        base::container::String(type.data(), type.size()),
        base::container::String(message.data(), message.size()));
    error.details_json = Json{{"success", false},
                              {"error_type", std::string(type)},
                              {"message", std::string(message)},
                              {"provider", "diagnostic_repair_plan"},
                              {"read_only", true}}
                             .dump();
    return error;
}

domain::AppError plan_error_from_context(const domain::AppError& context_error) {
    auto error = domain::AppError::invalid_argument(context_error.code, context_error.message);
    if (!context_error.details_json.empty()) {
        try {
            auto details = Json::parse(std::string(context_error.details_json.c_str()));
            if (details.is_object()) {
                details["provider"] = "diagnostic_repair_plan";
                details["read_only"] = true;
                error.details_json = details.dump();
                return error;
            }
        } catch (...) {
        }
    }
    error.details_json = Json{{"success", false},
                              {"error_type", std::string(context_error.code.c_str())},
                              {"message", std::string(context_error.message.c_str())},
                              {"provider", "diagnostic_repair_plan"},
                              {"read_only", true}}
                             .dump();
    return error;
}

std::string diagnostic_text(const Json& diagnostic) {
    if (!diagnostic.is_object()) return {};
    std::string text;
    auto append = [&text](const std::string& value) {
        if (value.empty()) return;
        if (!text.empty()) text += ' ';
        text += value;
    };
    append(diagnostic.value("source", ""));
    append(diagnostic.value("code", ""));
    append(diagnostic.value("severity", ""));
    append(diagnostic.value("message", ""));
    append(diagnostic.value("raw", ""));
    append(diagnostic.value("test_name", ""));
    return lower_copy(text);
}

std::string classify_issue(const Json& diagnostic) {
    auto text = diagnostic_text(diagnostic);
    if (contains_any(text, {"no such file", "module not found", "cannot find", "include file not found", "file not found"})) {
        return "missing_dependency";
    }
    if (contains_any(text, {"panic", "exception", "segmentation fault", "segfault", "timeout", "crash", "aborted"})) {
        return "runtime_error";
    }
    if (contains_any(text, {"gcc", "clang", "msvc", "compiler", "tsc", "undeclared", "not declared", "no member", "cannot convert", "undefined reference", "expected", "error"})) {
        return "compile_error";
    }
    if (contains_any(text, {"gtest", "pytest", "assert", "expect", "actual", "failed"})) {
        return "test_assertion";
    }
    return "unknown";
}

int severity_score(const Json& diagnostic) {
    auto severity = lower_copy(diagnostic.value("severity", ""));
    if (severity == "error" || severity == "failure" || severity == "fatal") return 60;
    if (severity == "warning") return 35;
    if (severity == "info") return 10;
    return 20;
}

Json safety_json() {
    return Json{{"read_only", true},
                {"requires_user_approval_before_edit", true},
                {"writes_files", false},
                {"runs_commands", false}};
}

std::map<std::string, int> file_counts_from_context(const Json& context) {
    std::map<std::string, int> counts;
    if (!context.contains("files") || !context["files"].is_array()) return counts;
    for (const auto& file : context["files"]) {
        if (!file.is_object()) continue;
        auto path = file.value("path", "");
        if (!path.empty()) counts[path] = file.value("diagnostic_count", 1);
    }
    return counts;
}

void add_candidate(Json& candidates,
                   std::set<std::string>& seen,
                   std::string path,
                   std::string reason,
                   int diagnostic_count) {
    if (path.empty() || !seen.insert(path).second) return;
    candidates.push_back(Json{{"path", std::move(path)},
                              {"reason", std::move(reason)},
                              {"diagnostic_count", diagnostic_count}});
}

Json build_candidate_files(const Json& item, const Json& context, const std::map<std::string, int>& file_counts) {
    Json candidates = Json::array();
    std::set<std::string> seen;
    const auto diagnostic = item.contains("diagnostic") ? item["diagnostic"] : Json::object();
    auto primary_path = diagnostic.value("path", "");
    if (!primary_path.empty()) {
        auto it = file_counts.find(primary_path);
        add_candidate(candidates, seen, primary_path, "primary diagnostic location", it == file_counts.end() ? 1 : it->second);
    }

    if (item.contains("definitions") && item["definitions"].is_array()) {
        for (const auto& definition : item["definitions"]) {
            if (!definition.is_object() || candidates.size() >= 5u) continue;
            auto path = definition.value("path", "");
            auto it = file_counts.find(path);
            add_candidate(candidates, seen, path, "definition referenced by diagnostic location", it == file_counts.end() ? 0 : it->second);
        }
    }

    if (context.contains("files") && context["files"].is_array()) {
        for (const auto& file : context["files"]) {
            if (!file.is_object() || candidates.size() >= 5u) continue;
            add_candidate(candidates, seen, file.value("path", ""), "same diagnostic batch", file.value("diagnostic_count", 1));
        }
    }
    return candidates;
}

Json copy_notes(const Json& item) {
    Json notes = Json::array();
    if (!item.contains("notes") || !item["notes"].is_array()) return notes;
    for (const auto& note : item["notes"]) {
        if (note.is_string()) notes.push_back(note.get<std::string>());
    }
    return notes;
}

Json build_evidence(const Json& item, const std::map<std::string, int>& file_counts) {
    Json evidence = Json::array();
    const auto diagnostic = item.contains("diagnostic") ? item["diagnostic"] : Json::object();
    auto path = diagnostic.value("path", "");
    auto line = diagnostic.value("line", 0);
    auto column = diagnostic.value("column", 0);
    if (!path.empty()) {
        std::string point = "Diagnostic points at " + path;
        if (line > 0) point += ":" + std::to_string(line);
        if (column > 0) point += ":" + std::to_string(column);
        point += ".";
        evidence.push_back(std::move(point));
    } else {
        evidence.push_back("Diagnostic does not have a workspace-local source path.");
    }

    if (item.contains("snippet") && item["snippet"].is_object()) {
        evidence.push_back("Source snippet is available around the failing line.");
    }
    if (!path.empty()) {
        auto it = file_counts.find(path);
        if (it != file_counts.end() && it->second > 1) {
            evidence.push_back("Multiple diagnostics point to the same file.");
        }
    }
    if (item.contains("symbols") && item["symbols"].is_array() && !item["symbols"].empty()) {
        evidence.push_back("Indexed symbols are available for the candidate file.");
    }
    if (item.contains("definitions") && item["definitions"].is_array() && !item["definitions"].empty()) {
        evidence.push_back("Best-effort definitions were found near the diagnostic location.");
    }
    if (item.contains("notes") && item["notes"].is_array()) {
        for (const auto& note : item["notes"]) {
            if (note.is_string()) evidence.push_back("Context note: " + note.get<std::string>() + ".");
        }
    }
    return evidence;
}

Json build_next_steps(const Json& item, const std::map<std::string, int>& file_counts) {
    Json steps = Json::array();
    const auto diagnostic = item.contains("diagnostic") ? item["diagnostic"] : Json::object();
    auto path = diagnostic.value("path", "");
    auto line = diagnostic.value("line", 0);
    if (!path.empty()) {
        Json inspect{{"kind", "inspect_source"},
                     {"title", "Inspect the failing line and surrounding scope"},
                     {"path", path}};
        if (line > 0) inspect["line"] = line;
        steps.push_back(std::move(inspect));
    }
    if (item.contains("symbols") && item["symbols"].is_array() && !item["symbols"].empty() && !path.empty()) {
        steps.push_back(Json{{"kind", "inspect_symbols"},
                             {"title", "Review indexed symbols in the candidate file"},
                             {"path", path},
                             {"line", line}});
    }
    if (item.contains("definitions") && item["definitions"].is_array() && !item["definitions"].empty()) {
        const auto& definition = item["definitions"][0];
        steps.push_back(Json{{"kind", "inspect_definition"},
                             {"title", "Inspect the nearest definition returned by code intelligence"},
                             {"path", definition.value("path", path)},
                             {"line", definition.value("line", 0)}});
    }
    auto it = file_counts.find(path);
    if (!path.empty() && it != file_counts.end() && it->second > 1) {
        steps.push_back(Json{{"kind", "compare_related_diagnostics"},
                             {"title", "Compare related diagnostics in the same file"},
                             {"path", path},
                             {"line", line}});
    }
    steps.push_back(Json{{"kind", "rerun_after_manual_fix"},
                         {"title", "Rerun the test command after an approved manual fix"}});
    return steps;
}

struct PlanDraft {
    Json plan;
    int score = 0;
    std::string path;
    std::string issue_type;
};

int issue_score(std::string_view issue_type) {
    if (issue_type == "compile_error") return 15;
    if (issue_type == "missing_dependency") return 12;
    if (issue_type == "test_assertion") return 10;
    if (issue_type == "runtime_error") return 8;
    return 0;
}

PlanDraft build_plan(const Json& item, const Json& context, const std::map<std::string, int>& file_counts) {
    const auto diagnostic = item.contains("diagnostic") ? item["diagnostic"] : Json::object();
    auto path = diagnostic.value("path", "");
    auto issue_type = classify_issue(diagnostic);
    auto file_count = 0;
    if (auto it = file_counts.find(path); it != file_counts.end()) file_count = it->second;

    int score = severity_score(diagnostic);
    if (!path.empty()) score += 15;
    if (item.contains("snippet") && item["snippet"].is_object()) score += 15;
    score += std::min(file_count, 5) * 5;
    score += diagnostic.value("confidence", 0) / 4;
    score += issue_score(issue_type);
    auto confidence = clamp_int(35 + score / 2, 35, 96);

    auto line = diagnostic.value("line", 0);
    std::string title = "Resolve ";
    if (issue_type == "compile_error") title += "compile diagnostic";
    else if (issue_type == "test_assertion") title += "test assertion diagnostic";
    else if (issue_type == "runtime_error") title += "runtime diagnostic";
    else if (issue_type == "missing_dependency") title += "missing dependency diagnostic";
    else title += "diagnostic";
    title += " in ";
    title += path.empty() ? "unknown location" : path;
    if (line > 0) title += ":" + std::to_string(line);

    Json plan{{"id", "plan-0"},
              {"rank", 0},
              {"title", std::move(title)},
              {"issue_type", issue_type},
              {"confidence", confidence},
              {"diagnostic", diagnostic},
              {"candidate_files", build_candidate_files(item, context, file_counts)},
              {"evidence", build_evidence(item, file_counts)},
              {"next_steps", build_next_steps(item, file_counts)},
              {"safety", safety_json()},
              {"notes", copy_notes(item)}};

    return PlanDraft{std::move(plan), score, path, issue_type};
}

} // namespace

DiagnosticRepairPlanService::DiagnosticRepairPlanService(
    workspace::WorkspaceContext ws_ctx,
    std::shared_ptr<diagnostic_context::DiagnosticContextService> context_service)
    : ws_ctx_(std::move(ws_ctx)), context_service_(std::move(context_service)) {
    if (!context_service_) {
        context_service_ = std::make_shared<diagnostic_context::DiagnosticContextService>(ws_ctx_);
    }
}

std::filesystem::path DiagnosticRepairPlanService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path() : cwd;
}

domain::AppResult<RepairPlanResult> DiagnosticRepairPlanService::repair_plan(const Json& request) const {
    auto request_session = workspace_index::RequestIndexSession(nullptr);
    return repair_plan(request, request_session);
}

domain::AppResult<RepairPlanResult> DiagnosticRepairPlanService::repair_plan(
    const Json& request,
    workspace_index::RequestIndexSession& request_session) const {
    if (!request.is_object()) {
        return domain::AppResult<RepairPlanResult>::failure(
            app_error("invalid_arguments", "request must be a JSON object"));
    }
    if (!context_service_) {
        return domain::AppResult<RepairPlanResult>::failure(
            app_error("service_unavailable", "diagnostic context service unavailable"));
    }

    auto context_request = diagnostic_context::repair_context_request_from_json(request);
    if (!context_request.ok()) {
        return domain::AppResult<RepairPlanResult>::failure(plan_error_from_context(context_request.error()));
    }

    auto context_result = context_service_->repair_context(std::move(context_request.value()), request_session);
    if (!context_result.ok()) {
        return domain::AppResult<RepairPlanResult>::failure(plan_error_from_context(context_result.error()));
    }
    auto context = diagnostic_context::to_json(context_result.value());

    auto file_counts = file_counts_from_context(context);
    std::vector<PlanDraft> drafts;
    if (context.contains("contexts") && context["contexts"].is_array()) {
        for (const auto& item : context["contexts"]) {
            if (!item.is_object()) continue;
            drafts.push_back(build_plan(item, context, file_counts));
        }
    }

    std::stable_sort(drafts.begin(), drafts.end(), [](const PlanDraft& lhs, const PlanDraft& rhs) {
        return lhs.score > rhs.score;
    });

    Json plans = Json::array();
    std::map<std::string, int> issue_counts;
    std::vector<std::string> primary_files;
    std::set<std::string> seen_files;
    int confidence_sum = 0;
    int rank = 1;
    for (auto& draft : drafts) {
        draft.plan["id"] = "plan-" + std::to_string(rank);
        draft.plan["rank"] = rank;
        confidence_sum += draft.plan.value("confidence", 0);
        issue_counts[draft.issue_type] += 1;
        if (!draft.path.empty() && seen_files.insert(draft.path).second) primary_files.push_back(draft.path);
        plans.push_back(std::move(draft.plan));
        ++rank;
    }

    std::string primary_issue = "unknown";
    int primary_count = -1;
    for (const auto& [issue, count] : issue_counts) {
        if (count > primary_count) {
            primary_issue = issue;
            primary_count = count;
        }
    }

    Json primary_files_json = Json::array();
    for (const auto& file : primary_files) {
        if (primary_files_json.size() >= 5u) break;
        primary_files_json.push_back(file);
    }

    auto plan_count = static_cast<int>(plans.size());
    auto summary_confidence = plan_count == 0 ? 0 : confidence_sum / plan_count;

    RepairPlanResult result;
    result.diagnostic_count = context.value("diagnostic_count", plan_count);
    result.plan_count = plan_count;
    result.truncated = context.value("truncated", false);
    result.summary = Json{{"primary_issue_type", primary_issue},
                          {"primary_files", primary_files_json},
                          {"confidence", summary_confidence}};
    result.plans = std::move(plans);
    return domain::AppResult<RepairPlanResult>::success(std::move(result));
}

Json to_json(const RepairPlanResult& result) {
    return Json{{"success", true},
                {"provider", "diagnostic_repair_plan"},
                {"read_only", true},
                {"diagnostic_count", result.diagnostic_count},
                {"plan_count", result.plan_count},
                {"truncated", result.truncated},
                {"summary", result.summary},
                {"plans", result.plans}};
}

} // namespace ben_gear::diagnostic_repair
