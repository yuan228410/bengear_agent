#include "ben_gear/server/composition/server_composition.hpp"

#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/result_presenter.hpp"
#include "ben_gear/server/composition/application_services.hpp"
#include "ben_gear/server/composition/command_api_composition.hpp"
#include "ben_gear/audit/audit_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace ben_gear::server::composition {

namespace {

workspace::WorkspaceContext workspace_context(ServerCompositionContext context,
                                               const container::String& workspace,
                                               const container::String& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context()
                         : workspace::WorkspaceContext{};
}

WorkspaceApplicationServices application_services(ServerCompositionContext context,
                                                  const container::String& workspace,
                                                  const container::String& username) {
    return WorkspaceApplicationServices(workspace_context(context, workspace, username));
}


int json_int_or(const Json& request, std::string_view key, int fallback) {
    auto it = request.find(std::string(key));
    if (it == request.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_string()) {
        try { return std::stoi(it->get<std::string>()); } catch (...) { return fallback; }
    }
    return fallback;
}

std::string json_string_or(const Json& request, std::string_view key, std::string fallback = {}) {
    auto it = request.find(std::string(key));
    if (it == request.end()) return fallback;
    return it->is_string() ? it->get<std::string>() : fallback;
}

repo_map::RepoMapService::Options workbench_repo_options(const Json& request) {
    repo_map::RepoMapService::Options options;
    options.max_files = std::clamp(json_int_or(request, "max_files", 2000), 1, 10000);
    options.max_symbols = std::clamp(json_int_or(request, "max_symbols", 5000), 1, 20000);
    options.max_dependencies = std::clamp(json_int_or(request, "max_dependencies", 0), 0, 20000);
    options.max_file_bytes = std::clamp(json_int_or(request, "max_file_bytes", 1024 * 1024), 1024, 4 * 1024 * 1024);
    options.include_external = request.value("include_external", false);
    options.include_hidden = request.value("include_hidden", false);
    options.refresh = request.value("refresh", false);
    return options;
}

code_intel::CodeIntelOptions workbench_code_options(const repo_map::RepoMapService::Options& options) {
    code_intel::CodeIntelOptions code_options;
    code_options.max_files = options.max_files;
    code_options.max_symbols = options.max_symbols;
    code_options.max_file_bytes = options.max_file_bytes;
    code_options.include_external = options.include_external;
    code_options.include_hidden = options.include_hidden;
    return code_options;
}

template <class Result, class ToJson>
Json workbench_result_json(domain::AppResult<Result> result, ToJson to_json) {
    if (!result.ok()) return app_error_json(result.error());
    return to_json(result.value());
}


std::filesystem::path weak_normal(std::filesystem::path path) {
    std::error_code ec;
    auto weak = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : weak;
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
    const auto& verification = snapshot["verification_context"];
    if (!verification.contains("commands") || !verification["commands"].is_array() || verification["commands"].empty()) return {};
    return verification["commands"][0].value("command", "");
}


Json impact_context_json(const Json& snapshot) {
    int dependency_count = 0;
    int dependent_count = 0;
    int related_test_count = 0;
    if (snapshot.contains("dependency_context") && snapshot["dependency_context"].is_object() && snapshot["dependency_context"].contains("summary")) {
        const auto& summary = snapshot["dependency_context"]["summary"];
        dependency_count = summary.value("dependency_count", 0);
        dependent_count = summary.value("dependent_count", 0);
        related_test_count = summary.value("related_test_count", 0);
    }

    int document_symbol_count = 0;
    int workspace_symbol_count = 0;
    if (snapshot.contains("symbol_context") && snapshot["symbol_context"].is_object() && snapshot["symbol_context"].contains("summary")) {
        const auto& summary = snapshot["symbol_context"]["summary"];
        document_symbol_count = summary.value("document_count", 0);
        workspace_symbol_count = summary.value("workspace_count", 0);
    }

    bool dirty = false;
    bool selected_has_diff = false;
    int changed_files = 0;
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object()) {
        const auto& change = snapshot["change_context"];
        if (change.contains("git_status") && change["git_status"].is_object()) {
            dirty = !change["git_status"].value("clean", true);
            if (change["git_status"].contains("entries") && change["git_status"]["entries"].is_array()) {
                changed_files = static_cast<int>(change["git_status"]["entries"].size());
            }
        }
        selected_has_diff = change.contains("diff") && change["diff"].is_object() && !change["diff"].value("diff", "").empty();
    }

    int diagnostic_count = 0;
    if (snapshot.contains("quality_context") && snapshot["quality_context"].is_object() &&
        snapshot["quality_context"].contains("diagnostic_context") && snapshot["quality_context"]["diagnostic_context"].is_object()) {
        diagnostic_count = snapshot["quality_context"]["diagnostic_context"].value("diagnostic_count", 0);
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

    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object() &&
        snapshot["verification_context"].contains("last_run") && snapshot["verification_context"]["last_run"].is_object() &&
        snapshot["verification_context"]["last_run"].value("provided", false)) {
        const auto& run = snapshot["verification_context"]["last_run"];
        status = run.value("status", "failed");
        command = run.value("command", "");
        diagnostic_count = run.value("diagnostic_count", 0);
        output_preview = run.value("output_preview", "");
    }

    if (status == "none" || status == "passed") {
        return Json{{"success", true}, {"read_only", true}, {"status", status}, {"failed", false}, {"diagnostics", diagnostics}, {"actions", actions}};
    }

    if (snapshot.contains("quality_context") && snapshot["quality_context"].is_object() &&
        snapshot["quality_context"].contains("diagnostic_context") && snapshot["quality_context"]["diagnostic_context"].is_object()) {
        const auto& ctx = snapshot["quality_context"]["diagnostic_context"];
        if (ctx.contains("contexts") && ctx["contexts"].is_array()) {
            int emitted = 0;
            for (const auto& item : ctx["contexts"]) {
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
    if (snapshot.contains("quality_context") && snapshot["quality_context"].is_object() &&
        snapshot["quality_context"].contains("diagnostic_context") && snapshot["quality_context"]["diagnostic_context"].is_object()) {
        diagnostic_count = snapshot["quality_context"]["diagnostic_context"].value("diagnostic_count", 0);
    }
    if (diagnostic_count > 0) {
        blockers.push_back(Json{{"kind", "diagnostics"}, {"message", "Diagnostics are present"}, {"count", diagnostic_count}, {"severity", "high"}});
        suggestions.push_back(Json{{"kind", "fix"}, {"title", "Resolve diagnostics before handoff"}});
    }

    std::string verification_status;
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object() &&
        snapshot["verification_context"].contains("last_run") && snapshot["verification_context"]["last_run"].is_object() &&
        snapshot["verification_context"]["last_run"].value("provided", false)) {
        verification_status = snapshot["verification_context"]["last_run"].value("status", "");
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
    if (snapshot.contains("impact_context") && snapshot["impact_context"].is_object() && snapshot["impact_context"].contains("metrics")) {
        selected_has_diff = snapshot["impact_context"]["metrics"].value("selected_has_diff", false);
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

    if (snapshot.contains("audit") && snapshot["audit"].is_object() && snapshot["audit"].contains("events") && snapshot["audit"]["events"].is_array()) {
        int emitted = 0;
        for (const auto& event : snapshot["audit"]["events"]) {
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

    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        if (snapshot["verification_context"].contains("last_run") && snapshot["verification_context"]["last_run"].is_object() &&
            snapshot["verification_context"]["last_run"].value("provided", false)) {
            const auto& run = snapshot["verification_context"]["last_run"];
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

    if (snapshot.contains("change_context") && snapshot["change_context"].is_object() &&
        snapshot["change_context"].contains("git_status") && snapshot["change_context"]["git_status"].is_object()) {
        bool clean = snapshot["change_context"]["git_status"].value("clean", true);
        int changed = 0;
        if (snapshot["change_context"]["git_status"].contains("entries") && snapshot["change_context"]["git_status"]["entries"].is_array()) {
            changed = static_cast<int>(snapshot["change_context"]["git_status"]["entries"].size());
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
    if (snapshot.contains("quality_context") && snapshot["quality_context"].is_object() &&
        snapshot["quality_context"].contains("diagnostic_context") && snapshot["quality_context"]["diagnostic_context"].is_object()) {
        diagnostic_count = snapshot["quality_context"]["diagnostic_context"].value("diagnostic_count", 0);
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

    if (snapshot.contains("symbol_context") && snapshot["symbol_context"].is_object() && snapshot["symbol_context"].contains("summary")) {
        const auto& sym_summary = snapshot["symbol_context"]["summary"];
        auto sym_total = sym_summary.value("document_count", 0) + sym_summary.value("workspace_count", 0);
        if (sym_total > 0) signals.push_back(Json{{"kind", "symbol"}, {"message", "Symbol source context is attached"}, {"count", sym_total}});
    }

    if (snapshot.contains("dependency_context") && snapshot["dependency_context"].is_object() && snapshot["dependency_context"].contains("summary")) {
        const auto& dep_summary = snapshot["dependency_context"]["summary"];
        auto dep_total = dep_summary.value("dependency_count", 0) + dep_summary.value("dependent_count", 0) + dep_summary.value("related_test_count", 0);
        if (dep_total > 0) signals.push_back(Json{{"kind", "dependency"}, {"message", "Dependency neighborhood is attached"}, {"count", dep_total}});
    }

    auto command = first_command_from_verification(snapshot);
    if (!command.empty()) {
        signals.push_back(Json{{"kind", "verification"}, {"message", "A verification command is recommended"}, {"command", command}});
    }

    Json top_actions = Json::array();
    if (snapshot.contains("action_context") && snapshot["action_context"].is_object() &&
        snapshot["action_context"].contains("actions") && snapshot["action_context"]["actions"].is_array()) {
        int copied = 0;
        for (const auto& action : snapshot["action_context"]["actions"]) {
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

    const auto& handoff = snapshot["handoff_context"];
    const auto& verification = snapshot["verification_context"];
    const auto& change = snapshot["change_context"];
    const auto& quality = snapshot["quality_context"];

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
        const auto& readiness = snapshot["readiness_context"];
        readiness_decision = readiness.value("decision", "go");
        readiness_level = readiness.value("level", "ready");
    }

    std::string review_status = "ready";
    int review_blockers = 0;
    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        const auto& review = snapshot["review_context"];
        review_status = review.value("status", "ready");
        review_blockers = review.value("blocker_count", 0);
    }

    bool failure = snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false);
    std::string failure_status = failure ? std::string(snapshot["failure_context"].value("status", "failed").c_str()) : std::string("none");

    std::string verification_status = "missing";
    std::string verification_command;
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        const auto& verification = snapshot["verification_context"];
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

    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object() &&
        snapshot["verification_context"].contains("last_run") && snapshot["verification_context"]["last_run"].is_object() &&
        snapshot["verification_context"]["last_run"].value("provided", false)) {
        const auto& run = snapshot["verification_context"]["last_run"];
        evidence.push_back(Json{{"kind", "verification_result"}, {"title", "Last verification"}, {"detail", run.value("status", "") + " / " + run.value("command", "")}});
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

    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object() && snapshot["verification_context"].contains("commands") && snapshot["verification_context"]["commands"].is_array()) {
        int emitted = 0;
        for (const auto& command : snapshot["verification_context"]["commands"]) {
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

Json verification_context_json(WorkspaceApplicationServices& services, const Json& snapshot, const Json& request) {
    Json repo_suggestions = Json::array();
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object() &&
        snapshot["change_context"].contains("test_suggestions") && snapshot["change_context"]["test_suggestions"].is_array()) {
        repo_suggestions = snapshot["change_context"]["test_suggestions"];
    }

    Json detected = workbench_result_json(services.test_loop()->inspect(), [](const test_loop::TestLoopInspectResult& result) {
        return test_loop::to_json(result);
    });

    Json commands = Json::array();
    std::set<std::string> seen_commands;
    auto append_command = [&](const Json& command) {
        if (!command.is_object()) return;
        auto text = command.value("command", "");
        if (text.empty() || seen_commands.count(text)) return;
        seen_commands.insert(text);
        commands.push_back(command);
    };
    for (const auto& item : repo_suggestions) append_command(item);
    if (detected.contains("suggestions") && detected["suggestions"].is_array()) {
        for (const auto& item : detected["suggestions"]) append_command(item);
    }

    int diagnostic_count = 0;
    bool diagnostics_provided = false;
    if (request.contains("diagnostics") && request["diagnostics"].is_array()) {
        diagnostic_count = static_cast<int>(request["diagnostics"].size());
        diagnostics_provided = diagnostic_count > 0;
    } else if (request.contains("diagnostic_output") && request["diagnostic_output"].is_string() && !request.value("diagnostic_output", "").empty()) {
        diagnostics_provided = true;
        if (snapshot.contains("quality_context") && snapshot["quality_context"].is_object() &&
            snapshot["quality_context"].contains("diagnostic_context") && snapshot["quality_context"]["diagnostic_context"].is_object()) {
            diagnostic_count = snapshot["quality_context"]["diagnostic_context"].value("diagnostic_count", 0);
        }
    }

    bool dirty = false;
    int changed_files = 0;
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object() &&
        snapshot["change_context"].contains("git_status") && snapshot["change_context"]["git_status"].is_object()) {
        dirty = !snapshot["change_context"]["git_status"].value("clean", true);
        if (snapshot["change_context"]["git_status"].contains("entries") && snapshot["change_context"]["git_status"]["entries"].is_array()) {
            changed_files = static_cast<int>(snapshot["change_context"]["git_status"]["entries"].size());
        }
    }

    Json last_run = Json{{"provided", false}};
    if (request.contains("verification_result") && request["verification_result"].is_object()) {
        const auto& run = request["verification_result"];
        auto success = run.value("success", false);
        auto exit_code = run.value("exit_code", -1);
        auto timed_out = run.value("timed_out", false);
        auto error_type = run.value("error_type", "");
        std::string status = "failed";
        if (success && exit_code == 0) status = "passed";
        else if (timed_out) status = "timeout";
        else if (error_type == "permission_required") status = "permission_required";
        else if (success) status = "completed";
        auto output = run.value("output", "");
        if (output.size() > 2000) output = output.substr(0, 2000);
        int run_diagnostic_count = run.contains("diagnostics") && run["diagnostics"].is_array()
                                       ? static_cast<int>(run["diagnostics"].size())
                                       : diagnostic_count;
        last_run = Json{{"provided", true},
                        {"status", status},
                        {"success", success},
                        {"exit_code", exit_code},
                        {"timed_out", timed_out},
                        {"error_type", error_type},
                        {"command", run.value("command", "")},
                        {"cwd", run.value("cwd", ".")},
                        {"elapsed_ms", run.value("elapsed_ms", 0)},
                        {"diagnostic_count", run_diagnostic_count},
                        {"output_preview", output}};
        diagnostics_provided = diagnostics_provided || run_diagnostic_count > 0 || !output.empty();
        diagnostic_count = std::max(diagnostic_count, run_diagnostic_count);
    }

    Json next = Json::array();
    if (last_run.value("provided", false) && last_run.value("status", "") != "passed") {
        next.push_back(Json{{"kind", "verification_result"}, {"title", "Inspect failed verification output"}, {"source", "test_loop"}});
    }
    if (diagnostic_count > 0) {
        next.push_back(Json{{"kind", "diagnostics"}, {"title", "Review diagnostic snippets before running broad tests"}, {"source", "quality_context"}});
    }
    if (!commands.empty()) {
        next.push_back(Json{{"kind", "command"}, {"title", "Run the highest-confidence verification command"}, {"command", commands[0].value("command", "")}, {"source", "test_loop"}});
    }
    if (dirty) {
        next.push_back(Json{{"kind", "diff"}, {"title", "Review changed files before final verification"}, {"source", "change_context"}});
    }

    return Json{{"success", true},
                {"read_only", true},
                {"commands", commands},
                {"detected", detected},
                {"diagnostics_provided", diagnostics_provided},
                {"diagnostic_count", diagnostic_count},
                {"last_run", last_run},
                {"dirty", dirty},
                {"changed_files", changed_files},
                {"next_steps", next}};
}

Json action_context_json(const Json& snapshot, const std::string& selected_path) {
    Json actions = Json::array();
    int priority = 100;

    const auto& quality = snapshot["quality_context"];
    if (quality.is_object() && quality.contains("diagnostic_context") && quality["diagnostic_context"].is_object()) {
        const auto& diagnostics = quality["diagnostic_context"];
        auto diagnostic_count = diagnostics.value("diagnostic_count", 0);
        if (diagnostic_count > 0) {
            std::string path;
            int line = 0;
            if (diagnostics.contains("contexts") && diagnostics["contexts"].is_array() && !diagnostics["contexts"].empty()) {
                const auto& first = diagnostics["contexts"][0];
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

    const auto& change = snapshot["change_context"];
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
        const auto& nav = snapshot["navigation_contexts"];
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

Json quality_context_json(WorkspaceApplicationServices& services,
                          const Json& request,
                          int context_lines,
                          int source_max_file_bytes,
                          const Json& test_suggestions) {
    Json quality{{"success", true}, {"diagnostic_context", Json{{"success", true}, {"diagnostic_count", 0}, {"truncated", false}, {"contexts", Json::array()}, {"files", Json::array()}}}, {"test_suggestions", test_suggestions}};
    auto has_diagnostics = request.contains("diagnostics") && request["diagnostics"].is_array() && !request["diagnostics"].empty();
    auto has_output = request.contains("diagnostic_output") && request["diagnostic_output"].is_string() && !request.value("diagnostic_output", "").empty();
    if (!has_diagnostics && !has_output) return quality;

    auto max_diagnostics = request.value("max_diagnostics", 20);
    if (max_diagnostics < 1) max_diagnostics = 1;
    if (max_diagnostics > 100) max_diagnostics = 100;
    Json ctx_request;
    ctx_request["context_lines"] = context_lines;
    ctx_request["max_diagnostics"] = max_diagnostics;
    ctx_request["max_file_bytes"] = source_max_file_bytes;
    ctx_request["include_code_intel"] = true;
    if (has_diagnostics) ctx_request["diagnostics"] = request["diagnostics"];
    if (has_output) ctx_request["output"] = request.value("diagnostic_output", "");
    if (request.contains("cwd")) ctx_request["cwd"] = request.value("cwd", ".");

    auto parsed = diagnostic_context::repair_context_request_from_json(ctx_request);
    if (!parsed.ok()) {
        quality["diagnostic_context"] = Json{{"success", false}, {"error_type", std::string(parsed.error().code.c_str())}, {"message", std::string(parsed.error().message.c_str())}};
        return quality;
    }
    quality["diagnostic_context"] = workbench_result_json(services.diagnostic_context()->repair_context(std::move(parsed.value())), [](const diagnostic_context::RepairContextResult& result) {
        return diagnostic_context::to_json(result);
    });
    return quality;
}

Json selected_git_entry(const Json& status, std::string_view path) {
    if (path.empty() || !status.contains("entries") || !status["entries"].is_array()) return Json();
    for (const auto& entry : status["entries"]) {
        if (!entry.is_object()) continue;
        if (entry.value("path", std::string()) == std::string(path)) return entry;
    }
    return Json();
}

Json test_suggestions_from_overview(const Json& overview) {
    if (overview.contains("summary") && overview["summary"].is_object() &&
        overview["summary"].contains("test_suggestions") && overview["summary"]["test_suggestions"].is_array()) {
        return overview["summary"]["test_suggestions"];
    }
    return Json::array();
}


Json handoff_package_json(const Json& snapshot) {
    Json package{{"success", true}, {"read_only", true}, {"package_version", 1}};

    std::string selected_path;
    if (snapshot.contains("agent_context") && snapshot["agent_context"].is_object()) {
        const auto& agent = snapshot["agent_context"];
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
        const auto& gate = snapshot["gate_context"];
        package["gate"] = Json{{"decision", gate.value("decision", "review")},
                                {"handoff_allowed", gate.value("handoff_allowed", false)},
                                {"blocker_count", gate.value("blocker_count", 0)},
                                {"verification_status", gate.value("verification_status", "missing")}};
        if (gate.contains("blockers")) package["gate"]["blockers"] = gate["blockers"];
        if (gate.contains("next_steps")) package["gate"]["next_steps"] = gate["next_steps"];
    }

    if (snapshot.contains("failure_context") && snapshot["failure_context"].is_object() && snapshot["failure_context"].value("failed", false)) {
        package["failure_context"] = snapshot["failure_context"];
    }
    if (snapshot.contains("verification_context") && snapshot["verification_context"].is_object()) {
        const auto& verification = snapshot["verification_context"];
        package["verification"] = Json{{"diagnostic_count", verification.value("diagnostic_count", 0)},
                                       {"changed_files", verification.value("changed_files", 0)},
                                       {"dirty", verification.value("dirty", false)}};
        if (verification.contains("last_run")) package["verification"]["last_run"] = verification["last_run"];
        if (verification.contains("commands")) package["verification"]["commands"] = verification["commands"];
    }
    if (snapshot.contains("review_context") && snapshot["review_context"].is_object()) {
        package["review_context"] = snapshot["review_context"];
    }
    if (snapshot.contains("timeline_context") && snapshot["timeline_context"].is_object()) {
        package["timeline_context"] = Json{{"next_step", snapshot["timeline_context"].value("next_step", "")},
                                           {"entry_count", snapshot["timeline_context"].value("entry_count", 0)}};
        if (snapshot["timeline_context"].contains("entries")) package["timeline_context"]["entries"] = snapshot["timeline_context"]["entries"];
    }
    if (snapshot.contains("change_context") && snapshot["change_context"].is_object()) {
        const auto& change = snapshot["change_context"];
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
    return package;
}

} // namespace

ApiServices make_api_services(ServerCompositionContext) {
    return ApiServices{};
}

RepoMapApiService make_repo_map_api_service(ServerCompositionContext context) {
    RepoMapApiService svc;
    svc.overview = [context](const container::String& workspace,
                             const container::String& username) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->overview(), [](const repo_map::RepoMapOverviewResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.find_files = [context](const container::String& workspace,
                               const container::String& username,
                               std::string_view query,
                               std::string_view kind,
                               std::string_view language,
                               int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->find_files(std::string(query), std::string(kind), std::string(language), limit), [](const repo_map::RepoMapFindFilesResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.find_symbols = [context](const container::String& workspace,
                                 const container::String& username,
                                 std::string_view query,
                                 std::string_view kind,
                                 std::string_view language,
                                 int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->find_symbols(std::string(query), std::string(kind), std::string(language), limit), [](const repo_map::RepoMapFindSymbolsResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.explain_path = [context](const container::String& workspace,
                                 const container::String& username,
                                 std::string_view path) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->explain_path(std::string(path)), [](const repo_map::RepoMapExplainPathResult& result) {
            return repo_map::to_json(result);
        });
    };
    return svc;
}

DiagnosticContextApiService make_diagnostic_context_api_service(ServerCompositionContext context) {
    DiagnosticContextApiService svc;
    svc.repair_context = [context](const container::String& workspace,
                                   const container::String& username,
                                   const Json& request) {
        auto services = application_services(context, workspace, username);
        auto parsed = diagnostic_context::repair_context_request_from_json(request);
        if (!parsed.ok()) return app_error_json(parsed.error());
        return app_result_json(services.diagnostic_context()->repair_context(std::move(parsed.value())), [](const diagnostic_context::RepairContextResult& result) {
            return diagnostic_context::to_json(result);
        });
    };
    return svc;
}

DiagnosticRepairApiService make_diagnostic_repair_api_service(ServerCompositionContext context) {
    DiagnosticRepairApiService svc;
    svc.repair_plan = [context](const container::String& workspace,
                                const container::String& username,
                                const Json& request) {
        auto services = application_services(context, workspace, username);
        auto parsed = diagnostic_repair::repair_plan_request_from_json(request);
        if (!parsed.ok()) return app_error_json(parsed.error());
        return app_result_json(services.diagnostic_repair_plan()->repair_plan(std::move(parsed.value())), [](const diagnostic_repair::RepairPlanResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };
    svc.repair_patch_preview = [context](const container::String& workspace,
                                         const container::String& username,
                                         const Json& request) {
        auto services = application_services(context, workspace, username);
        auto parsed = diagnostic_repair::repair_patch_preview_request_from_json(request);
        if (!parsed.ok()) return app_error_json(parsed.error());
        diagnostic_repair::DiagnosticRepairPatchPreviewService patch_preview(
            services.workspace_context(),
            services.diagnostic_repair_plan(),
            services.patch());
        return app_result_json(patch_preview.repair_patch_preview(std::move(parsed.value())), [](const diagnostic_repair::RepairPatchPreviewResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };
    svc.repair_workflow = [context](const container::String& workspace,
                                    const container::String& username,
                                    const Json& request) {
        auto enriched = request;
        if (!enriched.contains("username")) enriched["username"] = std::string(username.c_str());
        if (!enriched.contains("workspace")) enriched["workspace"] = std::string(context.workspace_resolver.workspace_or_default(workspace).c_str());
        auto parsed = diagnostic_repair::repair_workflow_request_from_json(enriched);
        if (!parsed.ok()) return app_error_json(parsed.error());
        auto pipeline = make_server_command_pipeline(CommandApiCompositionContext{context.workspace_resolver, context.session_pool});
        diagnostic_repair::DiagnosticRepairWorkflowService workflow(context.workspace_resolver, pipeline);
        return app_result_json(workflow.repair_workflow(parsed.value()), [](const diagnostic_repair::RepairWorkflowResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };
    return svc;
}

WorkbenchSnapshotApiService make_workbench_snapshot_api_service(ServerCompositionContext context) {
    WorkbenchSnapshotApiService svc;
    svc.snapshot = [context](const container::String& workspace,
                             const container::String& username,
                             const Json& request) {
        auto services = application_services(context, workspace, username);
        auto intelligence = services.code_intelligence_index();
        auto repo_options = workbench_repo_options(request);
        auto code_options = workbench_code_options(repo_options);
        auto limit = std::clamp(json_int_or(request, "limit", 50), 1, 200);
        auto audit_limit = std::clamp(json_int_or(request, "audit_limit", 20), 0, 100);
        auto context_lines = std::clamp(json_int_or(request, "context_lines", 8), 0, 50);
        auto source_max_file_bytes = std::clamp(json_int_or(request, "source_max_file_bytes", 256 * 1024), 1024, 2 * 1024 * 1024);
        auto max_location_contexts = std::clamp(json_int_or(request, "max_location_contexts", 8), 0, 50);
        auto path = json_string_or(request, "path");
        auto symbol = json_string_or(request, "symbol");
        auto query_text = json_string_or(request, "query", symbol);
        auto kind = json_string_or(request, "kind");
        auto language = json_string_or(request, "language");

        Json snapshot{{"success", true},
                      {"provider", "workbench"},
                      {"workspace", std::string(context.workspace_resolver.workspace_or_default(workspace).c_str())},
                      {"username", std::string(username.c_str())},
                      {"index", Json{{"request_scoped", true},
                                      {"shared_options", Json{{"max_files", repo_options.max_files},
                                                             {"max_symbols", repo_options.max_symbols},
                                                             {"max_dependencies", repo_options.max_dependencies},
                                                             {"include_external", repo_options.include_external},
                                                             {"include_hidden", repo_options.include_hidden},
                                                             {"refresh", repo_options.refresh}}},
                                      {"source_context_lines", context_lines}}}};

        snapshot["overview"] = workbench_result_json(intelligence->overview(repo_options), [](const repo_map::RepoMapOverviewResult& result) {
            return repo_map::to_json(result);
        });

        Json change_context{{"success", true}};
        auto git_status = git::to_json(services.git()->status());
        change_context["git_status"] = git_status;
        change_context["selected_file"] = Json::object();
        change_context["diff"] = Json{{"success", true}, {"diff", ""}, {"staged", false}, {"stat", false}};
        change_context["test_suggestions"] = test_suggestions_from_overview(snapshot["overview"]);
        if (!path.empty()) {
            auto selected = selected_git_entry(git_status, path);
            if (!selected.is_null()) change_context["selected_file"] = selected;
            change_context["diff"] = workbench_result_json(services.git()->diff(path, false, false), [](const git::GitDiffResult& result) {
                return git::to_json(result);
            });
        }
        snapshot["change_context"] = change_context;
        snapshot["quality_context"] = quality_context_json(services, request, context_lines, source_max_file_bytes, change_context["test_suggestions"]);
        if (!query_text.empty()) {
            snapshot["files"] = workbench_result_json(intelligence->find_files(query_text, {}, language, limit, repo_options), [](const repo_map::RepoMapFindFilesResult& result) {
                return repo_map::to_json(result);
            });
            snapshot["workspace_symbols"] = workbench_result_json(intelligence->workspace_symbols(query_text, kind, language, limit, code_options), [](const code_intel::CodeIntelWorkspaceSymbolsResult& result) {
                return code_intel::to_json(result);
            });
        }
        if (!path.empty()) {
            snapshot["path"] = workbench_result_json(intelligence->explain_path(path, repo_options), [](const repo_map::RepoMapExplainPathResult& result) {
                return repo_map::to_json(result);
            });
            snapshot["document_symbols"] = workbench_result_json(intelligence->document_symbols(path, code_options), [](const code_intel::CodeIntelDocumentSymbolsResult& result) {
                return code_intel::to_json(result);
            });
            auto focus_line = json_int_or(request, "line", 0);
            snapshot["source_context"] = source_context_json(std::filesystem::path(services.workspace_context().project_path.c_str()),
                                                               path,
                                                               focus_line,
                                                               context_lines,
                                                               source_max_file_bytes);
        }
        if (!symbol.empty() || (!path.empty() && json_int_or(request, "line", 0) > 0 && json_int_or(request, "column", 0) > 0)) {
            code_intel::CodeIntelQuery code_query;
            code_query.path = path;
            code_query.line = json_int_or(request, "line", 0);
            code_query.column = json_int_or(request, "column", 0);
            code_query.symbol = symbol;
            code_query.limit = limit;
            snapshot["definition"] = workbench_result_json(intelligence->definition(code_query, code_options), [](const code_intel::CodeIntelDefinitionResult& result) {
                return code_intel::to_json(result);
            });
            snapshot["references"] = workbench_result_json(intelligence->references(code_query, code_options), [](const code_intel::CodeIntelReferencesResult& result) {
                return code_intel::to_json(result);
            });
            auto project_root = std::filesystem::path(services.workspace_context().project_path.c_str());
            Json navigation_contexts = Json{{"success", true}, {"definition", Json::object()}, {"references", Json::object()}};
            if (max_location_contexts > 0 && snapshot["definition"].value("success", false)) {
                navigation_contexts["definition"] = source_contexts_from_locations(project_root,
                                                                                      snapshot["definition"].value("definitions", Json::array()),
                                                                                      "definition",
                                                                                      context_lines,
                                                                                      source_max_file_bytes,
                                                                                      max_location_contexts);
            }
            if (max_location_contexts > 0 && snapshot["references"].value("success", false)) {
                navigation_contexts["references"] = source_contexts_from_locations(project_root,
                                                                                      snapshot["references"].value("references", Json::array()),
                                                                                      "reference",
                                                                                      context_lines,
                                                                                      source_max_file_bytes,
                                                                                      max_location_contexts);
            }
            snapshot["navigation_contexts"] = navigation_contexts;
            snapshot["symbol_context"] = symbol_context_json(project_root, snapshot, context_lines, source_max_file_bytes, max_location_contexts);
            snapshot["dependency_context"] = dependency_context_json(project_root, snapshot["path"], context_lines, source_max_file_bytes, max_location_contexts);
        }
        if (!snapshot.contains("symbol_context")) snapshot["symbol_context"] = Json{{"success", true}, {"document", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}}, {"workspace", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}}, {"summary", Json{{"document_count", 0}, {"workspace_count", 0}}}};
        if (!snapshot.contains("dependency_context")) snapshot["dependency_context"] = Json{{"success", true}, {"dependencies", Json::array()}, {"dependents", Json::array()}, {"related_tests", Json::array()}, {"summary", Json{{"dependency_count", 0}, {"dependent_count", 0}, {"related_test_count", 0}}}};
        if (audit_limit > 0) {
            audit::AuditQuery audit_query;
            audit_query.workspace = context.workspace_resolver.workspace_or_default(workspace);
            audit_query.limit = audit_limit;
            audit::AuditStore store(context.workspace_resolver.user_dir_for(username) / "audit" / "events.jsonl");
            snapshot["audit"] = store.list(audit_query);
        } else {
            snapshot["audit"] = Json{{"success", true}, {"events", Json::array()}, {"truncated", false}};
        }
        snapshot["verification_context"] = verification_context_json(services, snapshot, request);
        snapshot["failure_context"] = failure_context_json(snapshot);
        snapshot["impact_context"] = impact_context_json(snapshot);
        snapshot["readiness_context"] = readiness_context_json(snapshot);
        snapshot["action_context"] = action_context_json(snapshot, path);
        snapshot["handoff_context"] = handoff_context_json(snapshot, path, query_text, symbol);
        snapshot["review_context"] = review_context_json(snapshot);
        snapshot["gate_context"] = gate_context_json(snapshot);
        snapshot["timeline_context"] = timeline_context_json(snapshot);
        snapshot["agent_context"] = agent_context_json(snapshot);
        snapshot["handoff_package"] = handoff_package_json(snapshot);
        return snapshot;
    };
    return svc;
}

CodeIntelApiService make_code_intel_api_service(ServerCompositionContext context) {
    CodeIntelApiService svc;
    svc.capabilities = [context](const container::String& workspace,
                                 const container::String& username) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->capabilities(), [](const code_intel::CodeIntelCapabilitiesResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.document_symbols = [context](const container::String& workspace,
                                     const container::String& username,
                                     std::string_view path) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->document_symbols(path), [](const code_intel::CodeIntelDocumentSymbolsResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.workspace_symbols = [context](const container::String& workspace,
                                      const container::String& username,
                                      std::string_view query,
                                      std::string_view kind,
                                      std::string_view language,
                                      int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->workspace_symbols(query, kind, language, limit), [](const code_intel::CodeIntelWorkspaceSymbolsResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.definition = [context](const container::String& workspace,
                               const container::String& username,
                               std::string_view path,
                               int line,
                               int column,
                               std::string_view symbol,
                               int limit) {
        auto services = application_services(context, workspace, username);
        code_intel::CodeIntelQuery query_value;
        query_value.path = std::string(path);
        query_value.line = line;
        query_value.column = column;
        query_value.symbol = std::string(symbol);
        query_value.limit = limit;
        return app_result_json(services.code_intel()->definition(query_value), [](const code_intel::CodeIntelDefinitionResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.references = [context](const container::String& workspace,
                               const container::String& username,
                               std::string_view path,
                               int line,
                               int column,
                               std::string_view symbol,
                               int limit) {
        auto services = application_services(context, workspace, username);
        code_intel::CodeIntelQuery query_value;
        query_value.path = std::string(path);
        query_value.line = line;
        query_value.column = column;
        query_value.symbol = std::string(symbol);
        query_value.limit = limit;
        return app_result_json(services.code_intel()->references(query_value), [](const code_intel::CodeIntelReferencesResult& result) {
            return code_intel::to_json(result);
        });
    };
    return svc;
}

void register_composed_api_routes(Router& router, ApiServices& services) {
    register_api_routes(router,
                        services.session,
                        services.config,
                        services.workspace,
                        services.mcp,
                        services.file,
                        services.git,
                        services.permission,
                        services.patch,
                        services.checkpoint,
                        services.test_loop,
                        services.diagnostic_context,
                        services.diagnostic_repair,
                        services.repo_map,
                        services.code_intel,
                        services.audit,
                        services.workbench);
}

} // namespace ben_gear::server::composition
