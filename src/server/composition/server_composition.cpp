#include "ben_gear/server/composition/server_composition.hpp"

#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/result_presenter.hpp"
#include "ben_gear/server/composition/application_services.hpp"
#include "ben_gear/server/composition/command_api_composition.hpp"
#include "ben_gear/audit/audit_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
        }
        if (audit_limit > 0) {
            audit::AuditQuery audit_query;
            audit_query.workspace = context.workspace_resolver.workspace_or_default(workspace);
            audit_query.limit = audit_limit;
            audit::AuditStore store(context.workspace_resolver.user_dir_for(username) / "audit" / "events.jsonl");
            snapshot["audit"] = store.list(audit_query);
        } else {
            snapshot["audit"] = Json{{"success", true}, {"events", Json::array()}, {"truncated", false}};
        }
        snapshot["action_context"] = action_context_json(snapshot, path);
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
