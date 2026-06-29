#include "ben_gear/server/composition/workbench_contexts.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::server::composition {

std::filesystem::path weak_normal(std::filesystem::path path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : normalized;
}

bool inside_root(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto root_string = root.generic_string();
    auto path_string = path.generic_string();
    if (root_string.empty()) return false;
    if (!root_string.empty() && root_string.back() != '/') root_string.push_back('/');
    if (path_string == root.generic_string()) return true;
    return path_string.rfind(root_string, 0) == 0;
}

std::vector<std::string> split_source_lines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    if (!content.empty() && content.back() == '\n') lines.emplace_back();
    return lines;
}

bool read_source_file_bounded(const std::filesystem::path& path,
                              std::int64_t max_file_bytes,
                              std::string& content,
                              std::string& error) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "file unavailable";
        return false;
    }
    if (size > static_cast<std::uintmax_t>(max_file_bytes)) {
        error = "file too large";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "file unavailable";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    if (static_cast<std::int64_t>(content.size()) > max_file_bytes) {
        error = "file too large";
        return false;
    }
    return true;
}

Json source_context_json(const std::filesystem::path& project_root,
                         std::string_view raw_path,
                         int focus_line,
                         int context_lines,
                         std::int64_t max_file_bytes) {
    if (raw_path.empty()) return Json{{"success", false}, {"error_type", "bad_request"}, {"message", "path is required"}};
    auto root = weak_normal(project_root);
    auto candidate = weak_normal(project_root / std::filesystem::path(std::string(raw_path)));
    if (!inside_root(root, candidate)) {
        return Json{{"success", false}, {"error_type", "workspace_escape"}, {"message", "path is outside workspace"}};
    }
    auto relative = candidate.lexically_relative(root).generic_string();
    std::string content;
    std::string error;
    if (!read_source_file_bounded(candidate, max_file_bytes, content, error)) {
        return Json{{"success", false}, {"error_type", "file_unavailable"}, {"message", error}, {"path", relative}};
    }
    auto lines = split_source_lines(content);
    if (focus_line <= 0) focus_line = lines.empty() ? 0 : 1;
    if (!lines.empty()) focus_line = std::clamp(focus_line, 1, static_cast<int>(lines.size()));
    auto start = lines.empty() ? 0 : std::max(1, focus_line - context_lines);
    auto end = lines.empty() ? 0 : std::min(static_cast<int>(lines.size()), focus_line + context_lines);
    Json out_lines = Json::array();
    for (int current = start; current <= end; ++current) {
        Json line{{"line", current}, {"text", lines[static_cast<size_t>(current - 1)]}};
        if (current == focus_line) line["primary"] = true;
        out_lines.push_back(std::move(line));
    }
    return Json{{"success", true},
                {"path", relative},
                {"start_line", start},
                {"end_line", end},
                {"focus_line", focus_line},
                {"total_lines", static_cast<int>(lines.size())},
                {"truncated", start > 1 || end < static_cast<int>(lines.size())},
                {"lines", out_lines}};
}


Json source_contexts_from_locations(const std::filesystem::path& project_root,
                                   const Json& locations,
                                   std::string_view kind,
                                   int context_lines,
                                   std::int64_t max_file_bytes,
                                   int max_items) {
    Json contexts = Json::array();
    if (!locations.is_array()) return Json{{"success", true}, {"contexts", contexts}, {"truncated", false}};
    int emitted = 0;
    for (const auto& location : locations) {
        if (!location.is_object()) continue;
        auto path = location.value("path", "");
        if (path.empty()) continue;
        auto line = location.value("line", 0);
        auto context = source_context_json(project_root, path, line, context_lines, max_file_bytes);
        Json entry{{"kind", std::string(kind)},
                   {"path", path},
                   {"line", line},
                   {"column", location.value("column", 0)},
                   {"symbol", location.value("symbol", "")},
                   {"context", context}};
        contexts.push_back(std::move(entry));
        ++emitted;
        if (emitted >= max_items) break;
    }
    return Json{{"success", true},
                {"contexts", contexts},
                {"truncated", static_cast<int>(locations.size()) > emitted}};
}






Json symbol_contexts_from_symbols(const std::filesystem::path& project_root,
                                  const Json& symbols,
                                  std::string kind,
                                  int context_lines,
                                  int source_max_file_bytes,
                                  int max_items) {
    Json contexts = Json::array();
    if (!symbols.is_array()) return Json{{"success", true}, {"contexts", contexts}, {"truncated", false}};
    int emitted = 0;
    for (const auto& symbol : symbols) {
        if (!symbol.is_object()) continue;
        if (emitted >= max_items) break;
        auto path = symbol.value("path", "");
        if (path.empty()) continue;
        Json item{{"kind", kind},
                  {"path", path},
                  {"line", symbol.value("line", 1)},
                  {"column", symbol.value("column", 1)},
                  {"symbol", symbol.value("symbol", "")},
                  {"symbol_kind", symbol.value("kind", "")},
                  {"signature", symbol.value("signature", "")},
                  {"container", symbol.value("container", "")}};
        item["context"] = source_context_json(project_root,
                                               path,
                                               symbol.value("line", 1),
                                               context_lines,
                                               source_max_file_bytes);
        contexts.push_back(item);
        ++emitted;
    }
    return Json{{"success", true}, {"contexts", contexts}, {"truncated", static_cast<int>(symbols.size()) > emitted}};
}

Json symbol_context_json(const std::filesystem::path& project_root,
                         const Json& snapshot,
                         int context_lines,
                         int source_max_file_bytes,
                         int max_items) {
    Json result{{"success", true},
                {"document", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}},
                {"workspace", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}},
                {"summary", Json{{"document_count", 0}, {"workspace_count", 0}}}};
    if (max_items <= 0) return result;
    if (snapshot.contains("document_symbols") && snapshot["document_symbols"].is_object() && snapshot["document_symbols"].value("success", false)) {
        result["document"] = symbol_contexts_from_symbols(project_root,
                                                           snapshot["document_symbols"].value("symbols", Json::array()),
                                                           "document_symbol",
                                                           context_lines,
                                                           source_max_file_bytes,
                                                           max_items);
    }
    if (snapshot.contains("workspace_symbols") && snapshot["workspace_symbols"].is_object() && snapshot["workspace_symbols"].value("success", false)) {
        result["workspace"] = symbol_contexts_from_symbols(project_root,
                                                            snapshot["workspace_symbols"].value("symbols", Json::array()),
                                                            "workspace_symbol",
                                                            context_lines,
                                                            source_max_file_bytes,
                                                            max_items);
    }
    result["summary"] = Json{{"document_count", static_cast<int>(result["document"].value("contexts", Json::array()).size())},
                             {"workspace_count", static_cast<int>(result["workspace"].value("contexts", Json::array()).size())}};
    return result;
}

Json source_context_for_path(const std::filesystem::path& project_root,
                             const Json& file,
                             int context_lines,
                             int source_max_file_bytes) {
    auto path = file.value("path", "");
    if (path.empty()) return Json{{"success", false}, {"error_type", "missing_path"}, {"message", "file path is missing"}};
    int line = file.value("line", 1);
    if (line <= 0) line = 1;
    return source_context_json(project_root, path, line, context_lines, source_max_file_bytes);
}

Json dependency_context_json(const std::filesystem::path& project_root,
                             const Json& path_explain,
                             int context_lines,
                             int source_max_file_bytes,
                             int max_items = 8) {
    Json result{{"success", true},
                {"dependencies", Json::array()},
                {"dependents", Json::array()},
                {"related_tests", Json::array()},
                {"summary", Json{{"dependency_count", 0}, {"dependent_count", 0}, {"related_test_count", 0}}}};
    if (!path_explain.is_object() || !path_explain.value("success", false)) return result;

    auto enrich_dep = [&](const Json& dep, bool dependent) {
        Json item = dep;
        auto context_path = dependent ? dep.value("from", "") : dep.value("resolved_path", "");
        if (!context_path.empty()) {
            item["context"] = source_context_json(project_root,
                                                   context_path,
                                                   dep.value("line", 1),
                                                   context_lines,
                                                   source_max_file_bytes);
        }
        return item;
    };
    auto copy_deps = [&](const char* key, bool dependent) {
        if (!path_explain.contains(key) || !path_explain[key].is_array()) return;
        int emitted = 0;
        for (const auto& dep : path_explain[key]) {
            if (emitted >= max_items) break;
            result[key].push_back(enrich_dep(dep, dependent));
            ++emitted;
        }
    };
    copy_deps("dependencies", false);
    copy_deps("dependents", true);

    if (path_explain.contains("related_tests") && path_explain["related_tests"].is_array()) {
        int emitted = 0;
        for (const auto& file : path_explain["related_tests"]) {
            if (emitted >= max_items) break;
            Json item = file;
            item["context"] = source_context_for_path(project_root, file, context_lines, source_max_file_bytes);
            result["related_tests"].push_back(item);
            ++emitted;
        }
    }
    result["summary"] = Json{{"dependency_count", static_cast<int>(result["dependencies"].size())},
                             {"dependent_count", static_cast<int>(result["dependents"].size())},
                             {"related_test_count", static_cast<int>(result["related_tests"].size())}};
    return result;
}

std::size_t json_array_size(const Json& value) {
    return value.is_array() ? value.size() : 0;
}

void push_action(Json& actions,
                 std::string id,
                 std::string kind,
                 std::string title,
                 std::string reason,
                 int priority,
                 std::string source,
                 std::string path = {},
                 int line = 0,
                 int column = 0,
                 std::string command = {}) {
    Json action{{"id", std::move(id)},
                {"kind", std::move(kind)},
                {"title", std::move(title)},
                {"reason", std::move(reason)},
                {"priority", priority},
                {"source", std::move(source)}};
    if (!path.empty()) action["path"] = std::move(path);
    if (line > 0) action["line"] = line;
    if (column > 0) action["column"] = column;
    if (!command.empty()) action["command"] = std::move(command);
    actions.push_back(action);
}



std::string first_command_from_verification(const Json& snapshot) {
    if (!snapshot.contains("verification_context") || !snapshot["verification_context"].is_object()) return {};
    const Json verification = snapshot["verification_context"];
    if (!verification.contains("commands") || !verification["commands"].is_array() || verification["commands"].empty()) return {};
    return verification["commands"][0].value("command", "");
}


Json impact_context_json(const Json& snapshot) {
    int dependency_count = 0;
    int dependent_count = 0;
    int related_test_count = 0;
    const Json dependency_context = snapshot["dependency_context"];
    if (dependency_context.is_object() && dependency_context.contains("summary")) {
        const Json summary = dependency_context["summary"];
        dependency_count = summary.value("dependency_count", 0);
        dependent_count = summary.value("dependent_count", 0);
        related_test_count = summary.value("related_test_count", 0);
    }

    int document_symbol_count = 0;
    int workspace_symbol_count = 0;
    const Json symbol_context = snapshot["symbol_context"];
    if (symbol_context.is_object() && symbol_context.contains("summary")) {
        const Json summary = symbol_context["summary"];
        document_symbol_count = summary.value("document_count", 0);
        workspace_symbol_count = summary.value("workspace_count", 0);
    }

    bool dirty = false;
    bool selected_has_diff = false;
    int changed_files = 0;
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object()) {
        const Json change = snapshot["change_context"];
        if (change.contains("git_status") && change["git_status"].is_object()) {
            dirty = !change["git_status"].value("clean", true);
            if (change["git_status"].contains("entries") && change["git_status"]["entries"].is_array()) {
                changed_files = static_cast<int>(change["git_status"]["entries"].size());
            }
        }
        selected_has_diff = change.contains("diff") && change["diff"].is_object() && !change["diff"].value("diff", "").empty();
    }

    int diagnostic_count = 0;
    const Json quality_context_for_impact = snapshot["quality_context"];
    const Json diagnostic_context_for_impact = quality_context_for_impact.is_object() ? quality_context_for_impact["diagnostic_context"] : Json();
    if (diagnostic_context_for_impact.is_object()) {
        diagnostic_count = diagnostic_context_for_impact.value("diagnostic_count", 0);
    }

    int score = dependent_count * 4 + dependency_count * 2 + related_test_count * 3 + document_symbol_count + workspace_symbol_count;
    if (selected_has_diff) score += 8;
    if (dirty) score += std::min(changed_files, 10);
    if (diagnostic_count > 0) score += diagnostic_count * 5;

    std::string level = "low";
    if (score >= 25) level = "high";
    else if (score >= 10) level = "medium";

    Json factors = Json::array();
    if (dependent_count > 0) factors.push_back(Json{{"kind", "dependents"}, {"count", dependent_count}, {"weight", 4}, {"message", "Files depend on the selected path"}});
    if (dependency_count > 0) factors.push_back(Json{{"kind", "dependencies"}, {"count", dependency_count}, {"weight", 2}, {"message", "Selected path depends on local files"}});
    if (related_test_count > 0) factors.push_back(Json{{"kind", "related_tests"}, {"count", related_test_count}, {"weight", 3}, {"message", "Related test files are available"}});
    if (document_symbol_count > 0) factors.push_back(Json{{"kind", "document_symbols"}, {"count", document_symbol_count}, {"weight", 1}, {"message", "Document symbols are available"}});
    if (selected_has_diff) factors.push_back(Json{{"kind", "selected_diff"}, {"count", 1}, {"weight", 8}, {"message", "Selected file has unstaged changes"}});
    if (diagnostic_count > 0) factors.push_back(Json{{"kind", "diagnostics"}, {"count", diagnostic_count}, {"weight", 5}, {"message", "Diagnostics are attached"}});

    Json recommended_focus = Json::array();
    if (diagnostic_count > 0) recommended_focus.push_back(Json{{"kind", "quality"}, {"title", "Resolve diagnostics first"}});
    if (selected_has_diff) recommended_focus.push_back(Json{{"kind", "change"}, {"title", "Review selected diff"}});
    if (dependent_count > 0) recommended_focus.push_back(Json{{"kind", "impact"}, {"title", "Inspect dependent files"}});
    if (related_test_count > 0) recommended_focus.push_back(Json{{"kind", "verification"}, {"title", "Run related tests"}});
    if (recommended_focus.empty()) recommended_focus.push_back(Json{{"kind", "explore"}, {"title", "Inspect source and symbols"}});

    return Json{{"success", true},
                {"read_only", true},
                {"score", score},
                {"level", level},
                {"metrics", Json{{"dependency_count", dependency_count},
                                  {"dependent_count", dependent_count},
                                  {"related_test_count", related_test_count},
                                  {"document_symbol_count", document_symbol_count},
                                  {"workspace_symbol_count", workspace_symbol_count},
                                  {"dirty", dirty},
                                  {"selected_has_diff", selected_has_diff},
                                  {"changed_files", changed_files},
                                  {"diagnostic_count", diagnostic_count}}},
                {"factors", factors},
                {"recommended_focus", recommended_focus}};
}



Json failure_context_json(const Json& snapshot) {
    Json diagnostics = Json::array();
    Json actions = Json::array();
    std::string status = "none";
    std::string command;
    int diagnostic_count = 0;
    std::string output_preview;

    const Json verification_context = snapshot["verification_context"];
    const Json last_run = verification_context.is_object() ? verification_context["last_run"] : Json();
    if (last_run.is_object() && last_run.value("provided", false)) {
        status = last_run.value("status", "failed");
        command = last_run.value("command", "");
        diagnostic_count = last_run.value("diagnostic_count", 0);
        output_preview = last_run.value("output_preview", "");
    }

    if (status == "none" || status == "passed") {
        return Json{{"success", true}, {"read_only", true}, {"status", status}, {"failed", false}, {"diagnostics", diagnostics}, {"actions", actions}};
    }

    const Json quality_context = snapshot["quality_context"];
    const Json diagnostic_context = quality_context.is_object() ? quality_context["diagnostic_context"] : Json();
    if (diagnostic_context.is_object()) {
        const Json ctx = diagnostic_context;
        const Json contexts = ctx["contexts"];
        if (contexts.is_array()) {
            int emitted = 0;
            for (const auto& item : contexts) {
                if (emitted >= 5) break;
                Json diag{{"path", item.value("path", "")},
                          {"line", item.value("line", 0)},
                          {"column", item.value("column", 0)},
                          {"message", item.value("message", "")},
                          {"severity", item.value("severity", "unknown")}};
                if (item.contains("snippet")) diag["snippet"] = item["snippet"];
                diagnostics.push_back(diag);
                ++emitted;
            }
        }
    }

    if (!diagnostics.empty()) {
        actions.push_back(Json{{"kind", "diagnostics"}, {"title", "Fix top diagnostic snippets first"}, {"source", "quality_context"}});
    }
    if (!output_preview.empty()) {
        actions.push_back(Json{{"kind", "output"}, {"title", "Inspect verification output preview"}, {"source", "verification_context"}});
    }
    actions.push_back(Json{{"kind", "rerun"}, {"title", "Rerun the same verification after fixes"}, {"command", command}, {"source", "test_loop"}});

    return Json{{"success", true},
                {"read_only", true},
                {"status", status},
                {"failed", true},
                {"command", command},
                {"diagnostic_count", diagnostic_count},
                {"output_preview", output_preview},
                {"diagnostics", diagnostics},
                {"actions", actions},
                {"brief", Json{{"title", "Verification failed"},
                                 {"status", status},
                                 {"command", command},
                                 {"diagnostic_count", diagnostic_count}}}};
}

Json readiness_context_json(const Json& snapshot) {
    Json blockers = Json::array();
    Json warnings = Json::array();
    Json suggestions = Json::array();

    int diagnostic_count = 0;
    const Json quality_context_for_readiness = snapshot["quality_context"];
    const Json diagnostic_context_for_readiness = quality_context_for_readiness.is_object() ? quality_context_for_readiness["diagnostic_context"] : Json();
    if (diagnostic_context_for_readiness.is_object()) {
        diagnostic_count = diagnostic_context_for_readiness.value("diagnostic_count", 0);
    }
    if (diagnostic_count > 0) {
        blockers.push_back(Json{{"kind", "diagnostics"}, {"message", "Diagnostics are present"}, {"count", diagnostic_count}, {"severity", "high"}});
        suggestions.push_back(Json{{"kind", "fix"}, {"title", "Resolve diagnostics before handoff"}});
    }

    std::string verification_status;
    const Json readiness_verification_context = snapshot["verification_context"];
    const Json readiness_last_run = readiness_verification_context.is_object() ? readiness_verification_context["last_run"] : Json();
    if (readiness_last_run.is_object() && readiness_last_run.value("provided", false)) {
        verification_status = readiness_last_run.value("status", "");
        if (verification_status != "passed") {
            blockers.push_back(Json{{"kind", "verification_failed"}, {"message", "Last verification did not pass"}, {"severity", "high"}, {"status", verification_status}});
            suggestions.push_back(Json{{"kind", "verification"}, {"title", "Inspect last verification output"}});
        } else {
            suggestions.push_back(Json{{"kind", "handoff"}, {"title", "Verification passed; prepare handoff or final review"}});
        }
    }

    bool dirty = false;
    int changed_files = 0;
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        dirty = snapshot["verification_context"].value("dirty", false);
        changed_files = snapshot["verification_context"].value("changed_files", 0);
    }
    bool selected_has_diff = false;
    const Json readiness_impact_context = snapshot["impact_context"];
    const Json readiness_impact_metrics = readiness_impact_context.is_object() ? readiness_impact_context["metrics"] : Json();
    if (readiness_impact_metrics.is_object()) {
        selected_has_diff = readiness_impact_metrics.value("selected_has_diff", false);
    }
    if (selected_has_diff) {
        warnings.push_back(Json{{"kind", "selected_diff"}, {"message", "Selected file has local changes"}, {"severity", "medium"}});
        suggestions.push_back(Json{{"kind", "review"}, {"title", "Review selected diff"}});
    } else if (dirty) {
        warnings.push_back(Json{{"kind", "dirty_workspace"}, {"message", "Workspace has local changes"}, {"count", changed_files}, {"severity", "medium"}});
    }

    std::string impact_level = "low";
    int impact_score = 0;
    if (snapshot.contains("impact_context") && snapshot["impact_context"].is_object()) {
        impact_level = snapshot["impact_context"].value("level", "low");
        impact_score = snapshot["impact_context"].value("score", 0);
    }
    if (impact_level == "high") {
        warnings.push_back(Json{{"kind", "high_impact"}, {"message", "Selected context has high impact"}, {"count", impact_score}, {"severity", "medium"}});
        suggestions.push_back(Json{{"kind", "scope"}, {"title", "Inspect dependents and related tests"}});
    }

    if (snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false)) {
        suggestions.push_back(Json{{"kind", "failure"}, {"title", "Use failure context to fix diagnostics/output before rerun"}});
    }

    auto command = first_command_from_verification(snapshot);
    if (command.empty()) {
        warnings.push_back(Json{{"kind", "no_verification_command"}, {"message", "No verification command is recommended"}, {"severity", "low"}});
        suggestions.push_back(Json{{"kind", "verification"}, {"title", "Identify a minimal verification command"}});
    } else {
        suggestions.push_back(Json{{"kind", "verification"}, {"title", "Run recommended verification"}, {"command", command}});
    }

    std::string level = "ready";
    std::string decision = "go";
    if (!blockers.empty()) {
        level = "blocked";
        decision = "no_go";
    } else if (!warnings.empty()) {
        level = "needs_review";
        decision = "review_first";
    }

    return Json{{"success", true},
                {"read_only", true},
                {"level", level},
                {"decision", decision},
                {"blocker_count", static_cast<int>(blockers.size())},
                {"warning_count", static_cast<int>(warnings.size())},
                {"blockers", blockers},
                {"warnings", warnings},
                {"suggestions", suggestions},
                {"brief", Json{{"title", level == "ready" ? "Ready for next step" : (level == "blocked" ? "Blocked before next step" : "Review before next step")},
                                 {"recommended_command", command},
                                 {"impact_level", impact_level},
                                 {"impact_score", impact_score},
                                 {"changed_files", changed_files},
                                 {"diagnostic_count", diagnostic_count},
                                 {"verification_status", verification_status}}}};
}


Json timeline_context_json(const Json& snapshot) {
    Json entries = Json::array();
    auto push_entry = [&](std::string kind, std::string title, std::string detail, std::string severity = "info", std::string ts = "") {
        Json entry{{"kind", std::move(kind)},
                   {"title", std::move(title)},
                   {"detail", std::move(detail)},
                   {"severity", std::move(severity)}};
        if (!ts.empty()) entry["ts"] = std::move(ts);
        entries.push_back(entry);
    };

    const Json audit = snapshot["audit"];
    const Json audit_events = audit.is_object() ? audit["events"] : Json();
    if (audit_events.is_array()) {
        int emitted = 0;
        for (const auto& event : audit_events) {
            if (emitted >= 6) break;
            auto category = event.value("category", "audit");
            auto action = event.value("action", "event");
            auto outcome = event.value("outcome", "");
            auto title = category + ":" + action;
            auto detail = outcome.empty() ? event.value("event_id", "") : outcome;
            auto severity = outcome == "denied" || outcome == "error" || outcome == "failed" ? "warning" : "info";
            push_entry("audit", title, detail, severity, event.value("ts", ""));
            ++emitted;
        }
    }

    if (snapshot.contains("readiness_context") && snapshot["readiness_context"].is_object()) {
        auto level = snapshot["readiness_context"].value("level", "ready");
        auto decision = snapshot["readiness_context"].value("decision", "go");
        auto severity = level == "blocked" ? "danger" : (level == "needs_review" ? "warning" : "success");
        push_entry("readiness", "Readiness: " + level, "Decision: " + decision, severity);
    }

    if (snapshot.contains("impact_context") && snapshot["impact_context"].is_object()) {
        auto level = snapshot["impact_context"].value("level", "low");
        auto score = snapshot["impact_context"].value("score", 0);
        auto severity = level == "high" ? "warning" : "info";
        push_entry("impact", "Impact: " + level, "Score " + std::to_string(score), severity);
    }

    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        auto status = snapshot["review_context"].value("status", "ready");
        auto blockers = snapshot["review_context"].value("blocker_count", 0);
        push_entry("review", "Review: " + status, "Blockers " + std::to_string(blockers), blockers > 0 ? "warning" : "success");
    }

    const Json timeline_verification_context = snapshot["verification_context"];
    if (timeline_verification_context.is_object()) {
        const Json run = timeline_verification_context["last_run"];
        if (run.is_object() && run.value("provided", false)) {
            auto status = run.value("status", "");
            auto severity = status == "passed" ? "success" : "danger";
            push_entry("verification_result", "Last verification: " + status, run.value("command", ""), severity);
        }
        if (snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false)) {
            push_entry("failure", "Failure context available", snapshot["failure_context"].value("command", ""), "danger");
        }
        if (snapshot.contains("gate_context") && snapshot["gate_context"].is_object()) {
            auto gate_decision = snapshot["gate_context"].value("decision", "review");
            push_entry("gate", "Review gate", gate_decision, gate_decision == "pass" ? "success" : (gate_decision == "blocked" ? "danger" : "warning"));
        }
        auto command = first_command_from_verification(snapshot);
        if (!command.empty()) push_entry("verification", "Recommended verification", command, "info");
    }

    const Json timeline_change_context = snapshot["change_context"];
    const Json timeline_git_status = timeline_change_context.is_object() ? timeline_change_context["git_status"] : Json();
    if (timeline_git_status.is_object()) {
        bool clean = timeline_git_status.value("clean", true);
        int changed = 0;
        const Json entries = timeline_git_status["entries"];
        if (entries.is_array()) {
            changed = static_cast<int>(entries.size());
        }
        push_entry("git", clean ? "Git workspace clean" : "Git workspace dirty", clean ? "No local changes" : std::to_string(changed) + " changed files", clean ? "success" : "warning");
    }

    std::string next_step = "Proceed";
    if (snapshot.contains("readiness_context") && snapshot["readiness_context"].is_object()) {
        auto decision = snapshot["readiness_context"].value("decision", "go");
        if (decision == "no_go") next_step = "Fix blockers before continuing";
        else if (decision == "review_first") next_step = "Review warnings then verify";
        else next_step = "Run verification or hand off";
    }

    return Json{{"success", true},
                {"read_only", true},
                {"entries", entries},
                {"entry_count", static_cast<int>(entries.size())},
                {"next_step", next_step}};
}

Json handoff_context_json(const Json& snapshot, const std::string& selected_path, const std::string& query_text, const std::string& symbol) {
    Json signals = Json::array();
    Json risks = Json::array();
    Json summary{{"success", true}, {"read_only", true}, {"selected_path", selected_path}, {"query", query_text}, {"symbol", symbol}};

    bool dirty = false;
    int changed_files = 0;
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        dirty = snapshot["verification_context"].value("dirty", false);
        changed_files = snapshot["verification_context"].value("changed_files", 0);
    }
    if (dirty) {
        signals.push_back(Json{{"kind", "change"}, {"message", "Workspace has uncommitted changes"}, {"count", changed_files}});
        risks.push_back(Json{{"kind", "dirty_workspace"}, {"message", "Review selected diff before final verification"}, {"severity", "medium"}});
    }

    int diagnostic_count = 0;
    const Json handoff_quality_context = snapshot["quality_context"];
    const Json handoff_diagnostic_context = handoff_quality_context.is_object() ? handoff_quality_context["diagnostic_context"] : Json();
    if (handoff_diagnostic_context.is_object()) {
        diagnostic_count = handoff_diagnostic_context.value("diagnostic_count", 0);
    }
    if (diagnostic_count > 0) {
        signals.push_back(Json{{"kind", "diagnostic"}, {"message", "Diagnostics are attached to this snapshot"}, {"count", diagnostic_count}});
        risks.push_back(Json{{"kind", "diagnostics_present"}, {"message", "Resolve diagnostics before broad refactors"}, {"severity", "high"}});
    }

    int action_count = 0;
    if (snapshot.contains("action_context") && snapshot["action_context"].is_object()) {
        action_count = snapshot["action_context"].value("action_count", 0);
    }
    if (action_count > 0) {
        signals.push_back(Json{{"kind", "action"}, {"message", "Prioritized action context is available"}, {"count", action_count}});
    }

    bool has_source = snapshot.contains("source_context") && snapshot["source_context"].is_object() && snapshot["source_context"].value("success", false);
    if (has_source) {
        signals.push_back(Json{{"kind", "source"}, {"message", "Selected source context is attached"}, {"count", 1}});
    }

    if (snapshot.contains("impact_context") && snapshot["impact_context"].is_object()) {
        signals.push_back(Json{{"kind", "impact"}, {"message", "Impact context is attached"}, {"count", snapshot["impact_context"].value("score", 0)}});
    }

    if (snapshot.contains("readiness_context") && snapshot["readiness_context"].is_object()) {
        signals.push_back(Json{{"kind", "readiness"}, {"message", "Readiness context is attached"}, {"count", snapshot["readiness_context"].value("blocker_count", 0)}});
    }

    if (snapshot.contains("timeline_context") && snapshot["timeline_context"].is_object()) {
        signals.push_back(Json{{"kind", "timeline"}, {"message", "Timeline context is attached"}, {"count", snapshot["timeline_context"].value("entry_count", 0)}});
    }

    const Json handoff_symbol_context = snapshot["symbol_context"];
    if (handoff_symbol_context.is_object() && handoff_symbol_context.contains("summary")) {
        const Json sym_summary = handoff_symbol_context["summary"];
        auto sym_total = sym_summary.value("document_count", 0) + sym_summary.value("workspace_count", 0);
        if (sym_total > 0) signals.push_back(Json{{"kind", "symbol"}, {"message", "Symbol source context is attached"}, {"count", sym_total}});
    }

    const Json handoff_dependency_context = snapshot["dependency_context"];
    if (handoff_dependency_context.is_object() && handoff_dependency_context.contains("summary")) {
        const Json dep_summary = handoff_dependency_context["summary"];
        auto dep_total = dep_summary.value("dependency_count", 0) + dep_summary.value("dependent_count", 0) + dep_summary.value("related_test_count", 0);
        if (dep_total > 0) signals.push_back(Json{{"kind", "dependency"}, {"message", "Dependency neighborhood is attached"}, {"count", dep_total}});
    }

    auto command = first_command_from_verification(snapshot);
    if (!command.empty()) {
        signals.push_back(Json{{"kind", "verification"}, {"message", "A verification command is recommended"}, {"command", command}});
    }

    Json top_actions = Json::array();
    const Json action_context = snapshot["action_context"];
    const Json actions = action_context.is_object() ? action_context["actions"] : Json();
    if (actions.is_array()) {
        int copied = 0;
        for (const auto& action : actions) {
            if (copied >= 3) break;
            top_actions.push_back(action);
            ++copied;
        }
    }

    std::string status = "ready";
    if (diagnostic_count > 0) status = "diagnostics";
    else if (dirty) status = "review_changes";
    else if (top_actions.empty() && command.empty()) status = "explore";

    summary["status"] = status;
    summary["signals"] = signals;
    summary["risks"] = risks;
    summary["top_actions"] = top_actions;
    summary["recommended_command"] = command;
    summary["brief"] = Json{{"title", selected_path.empty() ? "Workbench snapshot" : "Workbench snapshot for " + selected_path},
                             {"status", status},
                             {"changed_files", changed_files},
                             {"diagnostic_count", diagnostic_count},
                             {"action_count", action_count},
                             {"recommended_command", command}};
    return summary;
}


void push_review_item(Json& checklist,
                      std::string id,
                      std::string title,
                      std::string status,
                      std::string source,
                      std::string detail = {},
                      std::string severity = "info") {
    Json item{{"id", std::move(id)},
              {"title", std::move(title)},
              {"status", std::move(status)},
              {"source", std::move(source)},
              {"severity", std::move(severity)}};
    if (!detail.empty()) item["detail"] = std::move(detail);
    checklist.push_back(item);
}

Json review_context_json(const Json& snapshot) {
    Json checklist = Json::array();
    Json summary{{"success", true}, {"read_only", true}};

    const Json handoff = snapshot["handoff_context"];
    const Json verification = snapshot["verification_context"];
    const Json change = snapshot["change_context"];
    const Json quality = snapshot["quality_context"];

    std::string status = handoff.is_object() ? std::string(handoff.value("status", "ready").c_str()) : std::string("ready");
    std::string selected_path = handoff.is_object() ? std::string(handoff.value("selected_path", "").c_str()) : std::string();
    std::string recommended_command = handoff.is_object() ? std::string(handoff.value("recommended_command", "").c_str()) : std::string();
    auto diagnostic_count = verification.is_object() ? verification.value("diagnostic_count", 0) : 0;
    auto changed_files = verification.is_object() ? verification.value("changed_files", 0) : 0;
    auto dirty = verification.is_object() && verification.value("dirty", false);

    push_review_item(checklist,
                     "review-handoff-status",
                     "Confirm handoff status",
                     status == "diagnostics" ? "needs_attention" : "ready",
                     "handoff_context",
                     "Current status: " + status,
                     status == "diagnostics" ? "high" : "info");

    push_review_item(checklist,
                     "review-changes",
                     "Review workspace changes",
                     dirty ? "needs_attention" : "clean",
                     "change_context",
                     dirty ? std::to_string(changed_files) + " changed file(s)" : "Workspace is clean",
                     dirty ? "medium" : "info");

    push_review_item(checklist,
                     "review-diagnostics",
                     "Review diagnostics",
                     diagnostic_count > 0 ? "needs_attention" : "clean",
                     "quality_context",
                     diagnostic_count > 0 ? std::to_string(diagnostic_count) + " diagnostic(s) attached" : "No diagnostics attached",
                     diagnostic_count > 0 ? "high" : "info");

    push_review_item(checklist,
                     "review-verification",
                     "Confirm verification command",
                     recommended_command.empty() ? "missing" : "ready",
                     "verification_context",
                     recommended_command.empty() ? "No verification command detected" : recommended_command,
                     recommended_command.empty() ? "medium" : "info");

    int action_count = handoff.is_object() && handoff.contains("top_actions") && handoff["top_actions"].is_array()
                           ? static_cast<int>(handoff["top_actions"].size())
                           : 0;
    push_review_item(checklist,
                     "review-next-actions",
                     "Review top next actions",
                     action_count > 0 ? "ready" : "missing",
                     "action_context",
                     action_count > 0 ? std::to_string(action_count) + " top action(s)" : "No top actions available",
                     action_count > 0 ? "info" : "medium");

    bool has_diff = change.is_object() && change.contains("diff") && change["diff"].is_object() && !change["diff"].value("diff", "").empty();
    bool has_quality_context = quality.is_object() && quality.contains("diagnostic_context") && quality["diagnostic_context"].is_object() &&
                               quality["diagnostic_context"].contains("contexts") && quality["diagnostic_context"]["contexts"].is_array() &&
                               !quality["diagnostic_context"]["contexts"].empty();

    Json focus = Json::array();
    if (!selected_path.empty()) focus.push_back(Json{{"kind", "path"}, {"value", selected_path}});
    if (has_diff) focus.push_back(Json{{"kind", "diff"}, {"value", "selected file has diff"}});
    if (has_quality_context) focus.push_back(Json{{"kind", "diagnostics"}, {"value", diagnostic_count}});
    if (!recommended_command.empty()) focus.push_back(Json{{"kind", "verification"}, {"value", recommended_command}});

    int blockers = 0;
    for (const auto& item : checklist) {
        auto item_status = item.value("status", "");
        if (item_status == "needs_attention" || item_status == "missing") ++blockers;
    }

    std::string review_status = blockers > 0 ? "needs_review" : "ready";
    summary["status"] = review_status;
    summary["blocker_count"] = blockers;
    summary["checklist"] = checklist;
    summary["focus"] = focus;
    summary["brief"] = Json{{"title", selected_path.empty() ? "Review workbench snapshot" : "Review " + selected_path},
                             {"status", review_status},
                             {"handoff_status", status},
                             {"changed_files", changed_files},
                             {"diagnostic_count", diagnostic_count},
                             {"recommended_command", recommended_command}};
    return summary;
}


Json gate_context_json(const Json& snapshot) {
    Json gates = Json::array();
    Json blockers = Json::array();
    Json next_steps = Json::array();

    auto push_gate = [](Json& list, std::string id, std::string title, std::string status, std::string source, std::string detail = {}, std::string severity = "info") {
        Json item{{"id", std::move(id)},
                  {"title", std::move(title)},
                  {"status", std::move(status)},
                  {"source", std::move(source)},
                  {"severity", std::move(severity)}};
        if (!detail.empty()) item["detail"] = std::move(detail);
        list.push_back(item);
    };

    std::string readiness_decision = "go";
    std::string readiness_level = "ready";
    if (snapshot.contains("readiness_context") && snapshot["readiness_context"].is_object()) {
        const Json readiness = snapshot["readiness_context"];
        readiness_decision = readiness.value("decision", "go");
        readiness_level = readiness.value("level", "ready");
    }

    std::string review_status = "ready";
    int review_blockers = 0;
    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        const Json review = snapshot["review_context"];
        review_status = review.value("status", "ready");
        review_blockers = review.value("blocker_count", 0);
    }

    bool failure = snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false);
    std::string failure_status = failure ? std::string(snapshot["failure_context"].value("status", "failed").c_str()) : std::string("none");

    std::string verification_status = "missing";
    std::string verification_command;
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        const Json verification = snapshot["verification_context"];
        verification_command = first_command_from_verification(snapshot);
        if (verification.contains("last_run") && verification["last_run"].is_object() && verification["last_run"].value("provided", false)) {
            verification_status = verification["last_run"].value("status", "completed");
        }
    }

    push_gate(gates,
              "gate-readiness",
              "Readiness decision",
              readiness_decision == "go" ? "pass" : "block",
              "readiness_context",
              readiness_level + " / " + readiness_decision,
              readiness_decision == "go" ? "info" : "high");

    push_gate(gates,
              "gate-verification",
              "Verification evidence",
              verification_status == "passed" ? "pass" : (verification_status == "missing" ? "review" : "block"),
              "verification_context",
              verification_status == "missing" ? (verification_command.empty() ? "No verification result yet" : "Recommended: " + verification_command) : verification_status,
              verification_status == "passed" ? "info" : (verification_status == "missing" ? "medium" : "high"));

    push_gate(gates,
              "gate-review",
              "Review checklist",
              review_blockers == 0 ? "pass" : "review",
              "review_context",
              std::to_string(review_blockers) + " blocker(s)",
              review_blockers == 0 ? "info" : "medium");

    if (failure) {
        push_gate(gates,
                  "gate-failure",
                  "Failure context",
                  "block",
                  "failure_context",
                  failure_status,
                  "high");
    }

    for (const auto& item : gates) {
        auto status = item.value("status", "");
        if (status == "block" || status == "review") blockers.push_back(item);
    }

    std::string decision = "pass";
    std::string title = "Ready to hand off";
    bool handoff_allowed = true;
    if (failure || readiness_decision == "no_go" || verification_status == "failed" || verification_status == "timeout" || verification_status == "permission_required") {
        decision = "blocked";
        title = "Blocked before handoff";
        handoff_allowed = false;
        next_steps.push_back(Json{{"kind", "fix"}, {"title", "Fix failure context and rerun verification"}, {"source", "failure_context"}});
    } else if (readiness_decision != "go" || review_blockers > 0 || verification_status == "missing") {
        decision = "review";
        title = "Needs review before handoff";
        handoff_allowed = false;
        if (verification_status == "missing") next_steps.push_back(Json{{"kind", "verify"}, {"title", "Run recommended verification"}, {"command", verification_command}, {"source", "verification_context"}});
        if (review_blockers > 0) next_steps.push_back(Json{{"kind", "review"}, {"title", "Resolve review checklist items"}, {"source", "review_context"}});
    } else {
        next_steps.push_back(Json{{"kind", "handoff"}, {"title", "Proceed with handoff or final review"}, {"source", "agent_context"}});
    }

    return Json{{"success", true},
                {"read_only", true},
                {"decision", decision},
                {"title", title},
                {"handoff_allowed", handoff_allowed},
                {"gate_count", static_cast<int>(gates.size())},
                {"blocker_count", static_cast<int>(blockers.size())},
                {"readiness_decision", readiness_decision},
                {"review_status", review_status},
                {"verification_status", verification_status},
                {"gates", gates},
                {"blockers", blockers},
                {"next_steps", next_steps},
                {"brief", Json{{"title", title},
                                 {"decision", decision},
                                 {"handoff_allowed", handoff_allowed},
                                 {"blocker_count", static_cast<int>(blockers.size())},
                                 {"verification_status", verification_status}}}};
}

Json agent_context_json(const Json& snapshot) {
    Json constraints = Json::array();
    Json evidence = Json::array();
    Json commands = Json::array();

    constraints.push_back(Json{{"kind", "read_only"}, {"title", "Treat this context as read-only; do not mutate state from snapshot inspection."}});
    constraints.push_back(Json{{"kind", "workspace"}, {"title", "Stay inside the selected workspace and respect workspace-root boundaries."}});

    std::string selected_path;
    if (snapshot.contains("handoff_context") && snapshot["handoff_context"].is_object()) {
        selected_path = snapshot["handoff_context"].value("selected_path", "");
    }
    if (selected_path.empty() && snapshot.contains("path") && snapshot["path"].is_object()) {
        selected_path = snapshot["path"].value("path", "");
    }

    std::string readiness_level = "ready";
    std::string readiness_decision = "go";
    if (snapshot.contains("readiness_context") && snapshot["readiness_context"].is_object()) {
        readiness_level = snapshot["readiness_context"].value("level", "ready");
        readiness_decision = snapshot["readiness_context"].value("decision", "go");
        evidence.push_back(Json{{"kind", "readiness"}, {"title", "Readiness decision"}, {"detail", readiness_level + " / " + readiness_decision}});
    }

    const Json agent_verification_context = snapshot["verification_context"];
    const Json agent_last_run = agent_verification_context.is_object() ? agent_verification_context["last_run"] : Json();
    if (agent_last_run.is_object() && agent_last_run.value("provided", false)) {
        evidence.push_back(Json{{"kind", "verification_result"}, {"title", "Last verification"}, {"detail", agent_last_run.value("status", "") + " / " + agent_last_run.value("command", "")}});
    }

    if (snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false)) {
        evidence.push_back(Json{{"kind", "failure"}, {"title", "Failure context"}, {"detail", snapshot["failure_context"].value("status", "failed") + " / diagnostics " + std::to_string(snapshot["failure_context"].value("diagnostic_count", 0))}});
    }

    if (snapshot.contains("impact_context") && snapshot["impact_context"].is_object()) {
        auto impact_level = snapshot["impact_context"].value("level", "low");
        auto impact_score = snapshot["impact_context"].value("score", 0);
        evidence.push_back(Json{{"kind", "impact"}, {"title", "Impact assessment"}, {"detail", impact_level + " / score " + std::to_string(impact_score)}});
        if (impact_level == "high") constraints.push_back(Json{{"kind", "impact"}, {"title", "High-impact context: inspect dependents and tests before broad changes."}});
    }

    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        auto status = snapshot["review_context"].value("status", "ready");
        auto blockers = snapshot["review_context"].value("blocker_count", 0);
        evidence.push_back(Json{{"kind", "review"}, {"title", "Review status"}, {"detail", status + " / blockers " + std::to_string(blockers)}});
    }

    if (snapshot.contains("gate_context") && snapshot["gate_context"].is_object()) {
        auto decision = snapshot["gate_context"].value("decision", "review");
        auto blockers = snapshot["gate_context"].value("blocker_count", 0);
        evidence.push_back(Json{{"kind", "gate"}, {"title", "Review gate"}, {"detail", decision + " / blockers " + std::to_string(blockers)}});
        if (!snapshot["gate_context"].value("handoff_allowed", false)) {
            constraints.push_back(Json{{"kind", "gate"}, {"title", "Do not hand off as complete until gate blockers are resolved."}});
        }
    }

    if (snapshot.contains("timeline_context") && snapshot["timeline_context"].is_object()) {
        evidence.push_back(Json{{"kind", "timeline"}, {"title", "Timeline next step"}, {"detail", snapshot["timeline_context"].value("next_step", "Proceed")}});
    }

    const Json commands_context = snapshot["verification_context"];
    const Json verification_commands = commands_context.is_object() ? commands_context["commands"] : Json();
    if (verification_commands.is_array()) {
        int emitted = 0;
        for (const auto& command : verification_commands) {
            if (emitted >= 3) break;
            if (!command.is_object() || command.value("command", "").empty()) continue;
            commands.push_back(command);
            ++emitted;
        }
    }

    std::string objective = selected_path.empty() ? "Continue from the workbench snapshot." : "Continue work on " + selected_path + ".";
    if (readiness_decision == "no_go") objective = "Resolve blockers before continuing" + (selected_path.empty() ? std::string(".") : " on " + selected_path + ".");
    else if (readiness_decision == "review_first") objective = "Review warnings and verify before handoff" + (selected_path.empty() ? std::string(".") : " for " + selected_path + ".");

    auto command = first_command_from_verification(snapshot);
    std::string prompt = objective;
    prompt += " Use source, symbol, dependency, impact, readiness, review, and timeline contexts as evidence.";
    if (!command.empty()) prompt += " Recommended verification: `" + command + "`.";
    prompt += " Keep changes scoped and verify before reporting completion.";

    return Json{{"success", true},
                {"read_only", true},
                {"objective", objective},
                {"selected_path", selected_path},
                {"readiness_level", readiness_level},
                {"readiness_decision", readiness_decision},
                {"constraints", constraints},
                {"evidence", evidence},
                {"recommended_commands", commands},
                {"handoff_prompt", prompt},
                {"brief", Json{{"title", selected_path.empty() ? "Agent handoff" : "Agent handoff: " + selected_path},
                                 {"objective", objective},
                                 {"command", command},
                                 {"evidence_count", static_cast<int>(evidence.size())}}}};
}

Json action_context_json(const Json& snapshot, const std::string& selected_path) {
    Json actions = Json::array();
    int priority = 100;

    const Json quality = snapshot["quality_context"];
    if (quality.is_object() && quality.contains("diagnostic_context") && quality["diagnostic_context"].is_object()) {
        const Json diagnostics = quality["diagnostic_context"];
        auto diagnostic_count = diagnostics.value("diagnostic_count", 0);
        if (diagnostic_count > 0) {
            std::string path;
            int line = 0;
            if (diagnostics.contains("contexts") && diagnostics["contexts"].is_array() && !diagnostics["contexts"].empty()) {
                const Json contexts = diagnostics["contexts"];
                const Json first = contexts[0];
                if (first.contains("diagnostic") && first["diagnostic"].is_object()) {
                    path = first["diagnostic"].value("path", "");
                    line = first["diagnostic"].value("line", 0);
                }
            }
            push_action(actions,
                        "inspect-diagnostics",
                        "diagnostic",
                        "Inspect diagnostic repair context",
                        "Snapshot includes diagnostics with source snippets; fix these before broader changes.",
                        priority--,
                        "quality_context",
                        path.empty() ? selected_path : path,
                        line);
        }
    }

    const Json change = snapshot["change_context"];
    if (change.is_object()) {
        if (change.contains("diff") && change["diff"].is_object() && !change["diff"].value("diff", "").empty()) {
            push_action(actions,
                        "review-selected-diff",
                        "diff",
                        "Review selected file diff",
                        "The selected path has unstaged changes in the workspace snapshot.",
                        priority--,
                        "change_context",
                        selected_path);
        }
        if (change.contains("test_suggestions") && change["test_suggestions"].is_array() && !change["test_suggestions"].empty()) {
            const auto& test = change["test_suggestions"][0];
            push_action(actions,
                        "run-recommended-test",
                        "test",
                        "Run recommended verification",
                        test.value("reason", "Repo Map suggested a relevant verification command."),
                        priority--,
                        "change_context",
                        {},
                        0,
                        0,
                        test.value("command", ""));
        }
    }

    if (snapshot.contains("navigation_contexts") && snapshot["navigation_contexts"].is_object()) {
        const Json nav = snapshot["navigation_contexts"];
        auto definition_count = nav.contains("definition") && nav["definition"].is_object() ? json_array_size(nav["definition"].value("contexts", Json::array())) : 0;
        auto reference_count = nav.contains("references") && nav["references"].is_object() ? json_array_size(nav["references"].value("contexts", Json::array())) : 0;
        if (definition_count > 0 || reference_count > 0) {
            push_action(actions,
                        "inspect-navigation-contexts",
                        "navigation",
                        "Inspect definition/reference context pack",
                        "Code Intelligence returned navigation targets with inline source context.",
                        priority--,
                        "navigation_contexts",
                        selected_path);
        }
    }

    if (snapshot.contains("source_context") && snapshot["source_context"].is_object() && snapshot["source_context"].value("success", false)) {
        push_action(actions,
                    "read-source-context",
                    "source",
                    "Read selected source context",
                    "The snapshot includes bounded source around the selected file or location.",
                    priority--,
                    "source_context",
                    snapshot["source_context"].value("path", selected_path),
                    snapshot["source_context"].value("focus_line", 0));
    }

    if (snapshot.contains("audit") && snapshot["audit"].is_object()) {
        auto audit_count = snapshot["audit"].contains("events") ? json_array_size(snapshot["audit"].value("events", Json::array())) : 0;
        if (audit_count > 0) {
            push_action(actions,
                        "review-recent-audit",
                        "audit",
                        "Review recent workspace audit events",
                        "Recent audit events are available for this workspace snapshot.",
                        priority--,
                        "audit");
        }
    }

    return Json{{"success", true}, {"actions", actions}, {"action_count", static_cast<int>(actions.size())}, {"read_only", true}};
}

Json copy_array_limited(const Json& source, int limit, bool& truncated) {
    Json out = Json::array();
    truncated = false;
    if (!source.is_array()) return out;
    int copied = 0;
    for (const auto& item : source) {
        if (copied >= limit) {
            truncated = true;
            break;
        }
        out.push_back(item);
        ++copied;
    }
    return out;
}

Json handoff_package_json(const Json& snapshot) {
    Json package{{"success", true}, {"read_only", true}, {"package_version", 1}};
    Json truncation{{"commands", false}, {"timeline_entries", false}, {"review_checklist", false}, {"gate_blockers", false}, {"gate_next_steps", false}};
    package["schema"] = Json{{"name", "workbench_handoff_package"},
                              {"version", 1},
                              {"stability", "stable"},
                              {"description", "Read-only package for controlled human or agent handoff."}};

    std::string selected_path;
    if (snapshot.contains("agent_context") && snapshot["agent_context"].is_object()) {
        const Json agent = snapshot["agent_context"];
        selected_path = agent.value("selected_path", "");
        package["objective"] = agent.value("objective", "");
        package["selected_path"] = selected_path;
        package["agent_context"] = agent;
    }
    if (selected_path.empty() && snapshot.contains("path") && snapshot["path"].is_object()) {
        selected_path = snapshot["path"].value("path", "");
        package["selected_path"] = selected_path;
    }

    if (snapshot.contains("gate_context") && snapshot["gate_context"].is_object()) {
        const Json gate = snapshot["gate_context"];
        package["gate"] = Json{{"decision", gate.value("decision", "review")},
                                {"handoff_allowed", gate.value("handoff_allowed", false)},
                                {"blocker_count", gate.value("blocker_count", 0)},
                                {"verification_status", gate.value("verification_status", "missing")}};
        if (gate.contains("blockers")) {
            bool truncated = false;
            package["gate"]["blockers"] = copy_array_limited(gate["blockers"], 12, truncated);
            truncation["gate_blockers"] = truncated;
        }
        if (gate.contains("next_steps")) {
            bool truncated = false;
            package["gate"]["next_steps"] = copy_array_limited(gate["next_steps"], 8, truncated);
            truncation["gate_next_steps"] = truncated;
        }
    }

    if (snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false)) {
        package["failure_context"] = snapshot["failure_context"];
    }
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        const Json verification = snapshot["verification_context"];
        package["verification"] = Json{{"diagnostic_count", verification.value("diagnostic_count", 0)},
                                       {"changed_files", verification.value("changed_files", 0)},
                                       {"dirty", verification.value("dirty", false)}};
        if (verification.contains("last_run")) package["verification"]["last_run"] = verification["last_run"];
        if (verification.contains("commands")) {
            bool truncated = false;
            package["verification"]["commands"] = copy_array_limited(verification["commands"], 8, truncated);
            truncation["commands"] = truncated;
        }
    }
    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        package["review_context"] = snapshot["review_context"];
        if (package["review_context"].contains("checklist")) {
            bool truncated = false;
            package["review_context"]["checklist"] = copy_array_limited(package["review_context"]["checklist"], 12, truncated);
            truncation["review_checklist"] = truncated;
        }
    }
    if (snapshot.contains("timeline_context") && snapshot["timeline_context"].is_object()) {
        package["timeline_context"] = Json{{"next_step", snapshot["timeline_context"].value("next_step", "")},
                                           {"entry_count", snapshot["timeline_context"].value("entry_count", 0)}};
        if (snapshot["timeline_context"].contains("entries")) {
            bool truncated = false;
            package["timeline_context"]["entries"] = copy_array_limited(snapshot["timeline_context"]["entries"], 20, truncated);
            truncation["timeline_entries"] = truncated;
        }
    }
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object()) {
        const Json change = snapshot["change_context"];
        Json change_summary{{"success", change.value("success", false)}};
        if (change.contains("selected_file")) change_summary["selected_file"] = change["selected_file"];
        if (change.contains("git_status") && change["git_status"].is_object()) {
            change_summary["git_status"] = Json{{"clean", change["git_status"].value("clean", true)},
                                                {"branch", change["git_status"].value("branch", "")}};
        }
        package["change_summary"] = change_summary;
    }

    std::string gate_decision = package.contains("gate") && package["gate"].is_object() ? std::string(package["gate"].value("decision", "review").c_str()) : std::string("review");
    std::string title = selected_path.empty() ? "Workbench handoff package" : "Workbench handoff package: " + selected_path;
    std::string recommended_next_step = "Review package";
    if (package.contains("gate") && package["gate"].contains("next_steps") && package["gate"]["next_steps"].is_array() && !package["gate"]["next_steps"].empty()) {
        recommended_next_step = package["gate"]["next_steps"][0].value("title", "");
    }
    package["title"] = title;
    package["brief"] = Json{{"title", title},
                             {"gate_decision", gate_decision},
                             {"selected_path", selected_path},
                             {"recommended_next_step", recommended_next_step}};
    package["truncation"] = truncation;
    package["limits"] = Json{{"commands", 8}, {"timeline_entries", 20}, {"review_checklist", 12}, {"gate_blockers", 12}, {"gate_next_steps", 8}};
    return package;
}

} // namespace ben_gear::server::composition
