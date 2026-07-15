#include "server/composition/server_composition.hpp"

#include "server/composition/workbench_contexts.hpp"

#include "server/api/handlers.hpp"
#include "server/api/result_presenter.hpp"
#include "server/composition/application_services.hpp"
#include "server/composition/command_api_composition.hpp"
#include "capabilities/audit/audit_store.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_patch_draft_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_workflow_service.hpp"
#include "workspace/uuid.hpp"

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
                                               const std::string& workspace,
                                               const std::string& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context()
                         : workspace::WorkspaceContext{};
}

WorkspaceApplicationServices application_services(ServerCompositionContext context,
                                                  const std::string& workspace,
                                                  const std::string& username) {
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



std::string normalize_pack_path(const Json& diagnostic) {
    auto path = diagnostic.value("path", "");
    if (path.empty()) path = diagnostic.value("file", "");
    return path;
}

Json read_context_pack_file(const std::filesystem::path& file_path, std::string_view pack_id) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) return Json{{"success", false}, {"error_type", "context_pack_not_found"}, {"message", "context pack not found"}};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto pack = Json::parse(line);
            if (pack.is_object() && std::string(pack.value("context_pack_id", "")) == std::string(pack_id)) {
                return Json{{"success", true}, {"context_pack", pack}};
            }
        } catch (...) {
        }
    }
    return Json{{"success", false}, {"error_type", "context_pack_not_found"}, {"message", "context pack not found"}};
}

Json append_context_pack_file(const std::filesystem::path& file_path, Json pack) {
    try {
        std::filesystem::create_directories(file_path.parent_path());
        std::ofstream out(file_path, std::ios::app | std::ios::binary);
        if (!out) return Json{{"success", false}, {"error_type", "context_pack_write_failed"}, {"message", "failed to open context pack store"}};
        out << pack.dump() << '\n';
        return Json{{"success", true}, {"context_pack", pack}};
    } catch (const std::exception& e) {
        return Json{{"success", false}, {"error_type", "context_pack_write_failed"}, {"message", e.what()}};
    }
}

Json code_context_pack_json(WorkspaceApplicationServices& services, const Json& request) {
    auto intelligence = services.code_intelligence_index();
    auto limit_files = std::clamp(json_int_or(request, "max_files", 8), 1, 30);
    auto limit_symbols = std::clamp(json_int_or(request, "max_symbols", 20), 1, 200);
    auto context_lines = std::clamp(json_int_or(request, "context_lines", 5), 0, 40);
    code_intel::CodeIntelOptions options;
    options.max_files = std::clamp(json_int_or(request, "index_max_files", 2000), 1, 10000);
    options.max_symbols = std::clamp(json_int_or(request, "index_max_symbols", 5000), 1, 20000);

    std::vector<std::string> paths;
    if (request.contains("paths") && request["paths"].is_array()) {
        for (const auto& path : request["paths"]) {
            if (path.is_string() && static_cast<int>(paths.size()) < limit_files) paths.push_back(path.get<std::string>());
        }
    }
    if (request.contains("diagnostics") && request["diagnostics"].is_array()) {
        for (const auto& diagnostic : request["diagnostics"]) {
            auto path = normalize_pack_path(diagnostic);
            if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end() && static_cast<int>(paths.size()) < limit_files) {
                paths.push_back(path);
            }
        }
    }

    Json primary_files = Json::array();
    Json symbols = Json::array();
    Json definitions = Json::array();
    Json references = Json::array();
    Json snippets = Json::array();
    Json related_tests = Json::array();
    bool truncated = false;
    for (const auto& path : paths) {
        primary_files.push_back(path);
        auto document_symbols = intelligence->document_symbols(path, options);
        if (document_symbols.ok()) {
            auto symbol_json = code_intel::to_json(document_symbols.value()).value("symbols", Json::array());
            for (const auto& symbol : symbol_json) {
                if (symbols.size() >= static_cast<size_t>(limit_symbols)) { truncated = true; break; }
                symbols.push_back(symbol);
                auto name = symbol.value("name", "");
                if (!name.empty()) {
                    code_intel::CodeIntelQuery query;
                    query.symbol = name;
                    query.path = path;
                    query.line = symbol.value("line", 0);
                    query.column = symbol.value("column", 0);
                    auto defs = intelligence->definition(query, options);
                    if (defs.ok()) {
                        for (const auto& def : code_intel::to_json(defs.value()).value("definitions", Json::array())) definitions.push_back(def);
                    }
                    auto refs = intelligence->references(query, options);
                    if (refs.ok()) {
                        for (const auto& ref : code_intel::to_json(refs.value()).value("references", Json::array())) {
                            if (references.size() < static_cast<size_t>(limit_symbols * 2)) references.push_back(ref);
                        }
                    }
                }
            }
        }
        auto explain = intelligence->explain_path(path, repo_map::RepoMapService::Options{});
        if (explain.ok()) snippets.push_back(repo_map::to_json(explain.value()));
        auto filename = std::filesystem::path(path).filename().string();
        if (!filename.empty()) {
            auto tests = intelligence->find_files(filename, "test", "", 5, repo_map::RepoMapService::Options{});
            if (tests.ok()) related_tests = repo_map::to_json(tests.value()).value("files", Json::array());
        }
    }
    Json impact_summary{{"primary_file_count", static_cast<int>(primary_files.size())},
                        {"symbol_count", static_cast<int>(symbols.size())},
                        {"definition_count", static_cast<int>(definitions.size())},
                        {"reference_count", static_cast<int>(references.size())},
                        {"related_test_count", static_cast<int>(related_tests.size())},
                        {"context_lines", context_lines}};
    return Json{{"success", true},
                {"provider", "code_intel_context_pack"},
                {"primary_files", primary_files},
                {"symbols", symbols},
                {"definitions", definitions},
                {"references", references},
                {"related_tests", related_tests},
                {"snippets", snippets},
                {"impact_summary", impact_summary},
                {"truncated", truncated}};
}

bool workflow_can_resume(std::string_view status) {
    return status == "paused" || status == "failed";
}

bool workflow_can_cancel(std::string_view status) {
    return status == "created" || status == "running" || status == "paused" || status == "failed";
}

Json invalid_workflow_transition(std::string_view action, std::string_view status) {
    return Json{{"success", false},
                {"error_type", "invalid_workflow_transition"},
                {"message", std::string("cannot ") + std::string(action) + " workflow from status " + std::string(status)}};
}

Json workflow_timeline_projection(const Json& workflow) {
    Json nodes = Json::array();
    Json edges = Json::array();
    std::string previous;
    std::string failed;
    for (const auto& stage : workflow.value("stages", Json::array())) {
        auto id = stage.value("stage", "");
        auto status = stage.value("status", "");
        Json node{{"id", id}, {"label", id}, {"status", status}};
        if (stage.contains("execution_id")) node["execution_id"] = stage["execution_id"];
        if (stage.contains("error_type")) node["error_type"] = stage["error_type"];
        if (stage.contains("message")) node["message"] = stage["message"];
        nodes.push_back(std::move(node));
        if (!previous.empty()) edges.push_back(Json{{"from", previous}, {"to", id}});
        if (failed.empty() && status == "failed") failed = id;
        previous = id;
    }
    auto status = workflow.value("status", "");
    Json actions = Json::array();
    if (workflow_can_resume(status)) actions.push_back("resume");
    if (workflow_can_cancel(status)) actions.push_back("cancel");
    return Json{{"success", true},
                {"workflow_id", workflow.value("workflow_id", "")},
                {"status", status},
                {"current_node", workflow.value("current_stage", "")},
                {"failed_node", failed},
                {"nodes", nodes},
                {"edges", edges},
                {"actions", actions}};
}

Json workflow_integrity_report(const Json& workflow, const std::filesystem::path& user_dir) {
    Json checks = Json::array();
    Json warnings = Json::array();
    Json errors = Json::array();
    auto source = workflow.value("source_execution_id", "");
    audit::RuntimeExecutionStore executions(user_dir / "runtime" / "executions.jsonl");
    if (!source.empty()) {
        auto source_read = executions.get(source);
        checks.push_back(Json{{"name", "source_execution_exists"}, {"success", source_read.value("success", false)}, {"execution_id", source}});
        if (!source_read.value("success", false)) errors.push_back(Json{{"name", "missing_source_execution"}, {"execution_id", source}});
    } else {
        warnings.push_back(Json{{"name", "missing_source_execution_id"}});
    }
    for (const auto& stage : workflow.value("stages", Json::array())) {
        auto execution_id = stage.value("execution_id", "");
        if (execution_id.empty()) continue;
        auto read = executions.get(execution_id);
        checks.push_back(Json{{"name", "stage_execution_exists"}, {"stage", stage.value("stage", "")}, {"success", read.value("success", false)}, {"execution_id", execution_id}});
        if (!read.value("success", false)) errors.push_back(Json{{"name", "missing_stage_execution"}, {"stage", stage.value("stage", "")}, {"execution_id", execution_id}});
    }
    audit::RuntimeExecutionLinkQuery link_query;
    link_query.execution_id = source;
    link_query.limit = 1000;
    audit::RuntimeExecutionLinkStore links(user_dir / "runtime" / "links.jsonl");
    auto listed_links = links.list(link_query);
    if (listed_links.value("success", false)) {
        for (const auto& link : listed_links.value("links", Json::array())) {
            auto target = link.value("target_execution_id", "");
            if (target.empty()) continue;
            auto read = executions.get(target);
            checks.push_back(Json{{"name", "link_target_execution_exists"}, {"success", read.value("success", false)}, {"execution_id", target}});
            if (!read.value("success", false)) warnings.push_back(Json{{"name", "missing_link_target_execution"}, {"execution_id", target}});
        }
    }
    return Json{{"success", errors.empty()}, {"checks", checks}, {"warnings", warnings}, {"errors", errors}};
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
        Json run = request["verification_result"];
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
        last_run["provided"] = true;
        last_run["status"] = status;
        last_run["success"] = success;
        last_run["exit_code"] = exit_code;
        last_run["timed_out"] = timed_out;
        last_run["error_type"] = error_type;
        last_run["command"] = run.value("command", "");
        last_run["cwd"] = run.value("cwd", ".");
        last_run["elapsed_ms"] = run.value("elapsed_ms", 0);
        last_run["diagnostic_count"] = run_diagnostic_count;
        last_run["output_preview"] = output;
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

    // 逐步构建结果，避免大 initializer_list 在 MinGW 上触发 SIGSEGV
    Json result;
    result["success"] = true;
    result["read_only"] = true;
    result["commands"] = std::move(commands);
    result["detected"] = std::move(detected);
    result["diagnostics_provided"] = diagnostics_provided;
    result["diagnostic_count"] = diagnostic_count;
    result["last_run"] = std::move(last_run);
    result["dirty"] = dirty;
    result["changed_files"] = changed_files;
    result["next_steps"] = std::move(next);
    return result;
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
        quality["diagnostic_context"] = Json{{"success", false}, {"error_type", parsed.error().code}, {"message", parsed.error().message}};
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
    svc.overview = [context](const std::string& workspace,
                             const std::string& username) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->overview(), [](const repo_map::RepoMapOverviewResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.find_files = [context](const std::string& workspace,
                               const std::string& username,
                               std::string_view query,
                               std::string_view kind,
                               std::string_view language,
                               int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->find_files(std::string(query), std::string(kind), std::string(language), limit), [](const repo_map::RepoMapFindFilesResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.find_symbols = [context](const std::string& workspace,
                                 const std::string& username,
                                 std::string_view query,
                                 std::string_view kind,
                                 std::string_view language,
                                 int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.repo_map()->find_symbols(std::string(query), std::string(kind), std::string(language), limit), [](const repo_map::RepoMapFindSymbolsResult& result) {
            return repo_map::to_json(result);
        });
    };
    svc.explain_path = [context](const std::string& workspace,
                                 const std::string& username,
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
    svc.repair_context = [context](const std::string& workspace,
                                   const std::string& username,
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
    svc.repair_plan = [context](const std::string& workspace,
                                const std::string& username,
                                const Json& request) {
        auto services = application_services(context, workspace, username);
        auto parsed = diagnostic_repair::repair_plan_request_from_json(request);
        if (!parsed.ok()) return app_error_json(parsed.error());
        return app_result_json(services.diagnostic_repair_plan()->repair_plan(std::move(parsed.value())), [](const diagnostic_repair::RepairPlanResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };
    svc.repair_patch_preview = [context](const std::string& workspace,
                                         const std::string& username,
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

    svc.repair_patch_draft = [context](const std::string& workspace,
                                       const std::string& username,
                                       const Json& request) {
        auto services = application_services(context, workspace, username);
        auto parsed = diagnostic_repair::repair_patch_draft_request_from_json(request);
        if (!parsed.ok()) return app_error_json(parsed.error());
        diagnostic_repair::DiagnosticRepairPatchPreviewService patch_preview(
            services.workspace_context(),
            services.diagnostic_repair_plan(),
            services.patch());
        diagnostic_repair::DiagnosticRepairPatchDraftService patch_draft(services.workspace_context(), std::make_shared<diagnostic_repair::DiagnosticRepairPatchPreviewService>(std::move(patch_preview)));
        return app_result_json(patch_draft.repair_patch_draft(std::move(parsed.value())), [](const diagnostic_repair::RepairPatchDraftResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };

    svc.repair_workflow = [context](const std::string& workspace,
                                    const std::string& username,
                                    const Json& request) {
        auto enriched = request;
        if (!enriched.contains("username")) enriched["username"] = username;
        if (!enriched.contains("workspace")) enriched["workspace"] = context.workspace_resolver.workspace_or_default(workspace);
        auto parsed = diagnostic_repair::repair_workflow_request_from_json(enriched);
        if (!parsed.ok()) return app_error_json(parsed.error());
        auto pipeline = make_server_command_pipeline(CommandApiCompositionContext{context.workspace_resolver, context.session_pool});
        auto repair_svc = diagnostic_repair::DiagnosticRepairWorkflowService(context.workspace_resolver, pipeline);
        return app_result_json(repair_svc.repair_workflow(parsed.value()), [](const diagnostic_repair::RepairWorkflowResult& result) {
            return diagnostic_repair::to_json(result);
        });
    };
    return svc;
}


RuntimeApiService make_runtime_api_service(ServerCompositionContext context) {
    RuntimeApiService svc;
    svc.list_executions = [context](const std::string& workspace,
                                    const std::string& session_id,
                                    const std::string& username,
                                    const std::string& action,
                                    const std::string& status,
                                    const std::string& capability,
                                    int limit) {
        audit::RuntimeExecutionQuery query;
        query.workspace = context.workspace_resolver.workspace_or_default(workspace);
        query.session_id = session_id;
        query.username = username;
        query.action = action;
        query.status = status;
        query.capability = capability;
        query.limit = limit;
        audit::RuntimeExecutionStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "executions.jsonl");
        return store.list(query);
    };
    svc.read_execution = [context](const std::string& username,
                                   const std::string& execution_id) {
        audit::RuntimeExecutionStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "executions.jsonl");
        return store.get(execution_id);
    };
    svc.list_links = [context](const std::string& workspace,
                               const std::string& session_id,
                               const std::string& username,
                               const std::string& execution_id,
                               const std::string& relation,
                               int limit) {
        audit::RuntimeExecutionLinkQuery query;
        query.workspace = context.workspace_resolver.workspace_or_default(workspace);
        query.session_id = session_id;
        query.username = username;
        query.execution_id = execution_id;
        query.relation = relation;
        query.limit = limit;
        audit::RuntimeExecutionLinkStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "links.jsonl");
        return store.list(query);
    };
    svc.append_link = [context](const std::string& workspace,
                                const std::string& session_id,
                                const std::string& username,
                                const std::string& source_execution_id,
                                const Json& body) {
        Json link = body.is_object() ? body : Json::object();
        link["workspace"] = context.workspace_resolver.workspace_or_default(workspace);
        link["session_id"] = session_id;
        link["username"] = username;
        link["source_execution_id"] = source_execution_id;
        if (!link.contains("relation") || link.value("relation", "").empty()) link["relation"] = "related";
        audit::RuntimeExecutionLinkStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "links.jsonl");
        return store.append(std::move(link));
    };
    svc.list_workflows = [context](const std::string& workspace,
                                   const std::string& session_id,
                                   const std::string& username,
                                   const std::string& status,
                                   const std::string& source_execution_id,
                                   int limit) {
        audit::RuntimeWorkflowQuery query;
        query.workspace = context.workspace_resolver.workspace_or_default(workspace);
        query.session_id = session_id;
        query.username = username;
        query.status = status;
        query.source_execution_id = source_execution_id;
        query.limit = limit;
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        return store.list(query);
    };
    svc.read_workflow = [context](const std::string& username,
                                  const std::string& workflow_id) {
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        return store.get(workflow_id);
    };
    svc.start_repair_workflow = [context](const std::string& workspace,
                                          const std::string& session_id,
                                          const std::string& username,
                                          const Json& body) {
        auto resolved_workspace = context.workspace_resolver.workspace_or_default(workspace);
        Json workflow{{"kind", "repair"},
                      {"workspace", resolved_workspace},
                      {"session_id", session_id},
                      {"username", username},
                      {"source_execution_id", body.value("source_execution_id", body.value("runtime_execution_id", ""))},
                      {"status", "running"},
                      {"current_stage", "repair_plan"},
                      {"request", body},
                      {"stages", Json::array({Json{{"stage", "repair_plan"}, {"status", "pending"}},
                                               Json{{"stage", "context_pack"}, {"status", body.contains("code_context") ? "succeeded" : "optional"}},
                                               Json{{"stage", "draft_patch"}, {"status", body.contains("unified_diff") ? "skipped" : "pending"}},
                                               Json{{"stage", "patch_preview"}, {"status", body.contains("unified_diff") ? "pending" : "blocked"}},
                                               Json{{"stage", "checkpoint"}, {"status", body.contains("unified_diff") ? "pending" : "blocked"}},
                                               Json{{"stage", "patch_apply"}, {"status", body.contains("unified_diff") ? "pending" : "blocked"}},
                                               Json{{"stage", "verification_rerun"}, {"status", body.value("command", "").empty() ? "blocked" : "pending"}},
                                               Json{{"stage", "finalize"}, {"status", "pending"}}})}};
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        auto created = store.append(workflow);
        if (!created.value("success", false)) return created;
        auto workflow_id = created["workflow"].value("workflow_id", "");

        Json final_patch;
        if (!body.contains("unified_diff") || body.value("unified_diff", "").empty()) {
            auto services = application_services(context, resolved_workspace, username);
            auto draft_request = body;
            draft_request["workspace"] = resolved_workspace;
            auto parsed_draft = diagnostic_repair::repair_patch_draft_request_from_json(draft_request);
            if (!parsed_draft.ok()) {
                final_patch = Json{{"status", "paused"},
                                   {"current_stage", "draft_patch"},
                                   {"summary", Json{{"message", "repair workflow paused; patch draft request could not be parsed"},
                                                     {"needs_patch_candidate", true},
                                                     {"error_type", parsed_draft.error().code},
                                                     {"error_message", parsed_draft.error().message}}}};
            } else {
                diagnostic_repair::DiagnosticRepairPatchPreviewService patch_preview(
                    services.workspace_context(),
                    services.diagnostic_repair_plan(),
                    services.patch());
                diagnostic_repair::DiagnosticRepairPatchDraftService patch_draft(
                    services.workspace_context(),
                    std::make_shared<diagnostic_repair::DiagnosticRepairPatchPreviewService>(std::move(patch_preview)));
                auto draft = patch_draft.repair_patch_draft(std::move(parsed_draft.value()));
                if (!draft.ok()) {
                    final_patch = Json{{"status", "paused"},
                                       {"current_stage", "draft_patch"},
                                       {"summary", Json{{"message", "repair workflow paused; patch draft failed"},
                                                         {"needs_patch_candidate", true},
                                                         {"error_type", draft.error().code},
                                                         {"error_message", draft.error().message}}}};
                } else {
                    auto draft_json = diagnostic_repair::to_json(draft.value());
                    auto drafted = draft_json.value("drafted", false);
                    final_patch = Json{{"status", "paused"},
                                       {"current_stage", drafted ? "patch_preview" : "draft_patch"},
                                       {"patch_draft", draft_json},
                                       {"summary", Json{{"message", drafted ? "repair workflow generated a patch draft and is awaiting confirmation" : "repair workflow paused until a patch candidate is provided"},
                                                         {"needs_patch_candidate", !drafted},
                                                         {"awaiting_user_confirmation", drafted}}},
                                       {"stages", Json::array({Json{{"stage", "repair_plan"}, {"status", "succeeded"}},
                                                               Json{{"stage", "context_pack"}, {"status", body.contains("code_context") ? "succeeded" : "optional"}},
                                                               Json{{"stage", "draft_patch"}, {"status", drafted ? "succeeded" : "blocked"}},
                                                               Json{{"stage", "patch_preview"}, {"status", drafted ? "succeeded" : "blocked"}},
                                                               Json{{"stage", "checkpoint"}, {"status", "blocked"}},
                                                               Json{{"stage", "patch_apply"}, {"status", "blocked"}},
                                                               Json{{"stage", "verification_rerun"}, {"status", "blocked"}},
                                                               Json{{"stage", "finalize"}, {"status", "blocked"}}})}};
                }
            }
        } else {
            auto enriched = body;
            enriched["workspace"] = resolved_workspace;
            enriched["session_id"] = session_id;
            enriched["username"] = username;
            auto parsed = diagnostic_repair::repair_workflow_request_from_json(enriched);
            if (!parsed.ok()) {
                final_patch = Json{{"status", "failed"},
                                   {"current_stage", "repair_plan"},
                                   {"error_type", parsed.error().code},
                                   {"message", parsed.error().message}};
            } else {
                auto pipeline = make_server_command_pipeline(CommandApiCompositionContext{context.workspace_resolver, context.session_pool});
                diagnostic_repair::DiagnosticRepairWorkflowService repair(context.workspace_resolver, pipeline);
                auto result = repair.repair_workflow(parsed.value());
                if (!result.ok()) {
                    final_patch = Json{{"status", "failed"},
                                       {"current_stage", "finalize"},
                                       {"error_type", result.error().code},
                                       {"message", result.error().message}};
                } else {
                    auto repair_json = diagnostic_repair::to_json(result.value());
                    final_patch = Json{{"status", repair_json.value("success", false) ? "succeeded" : "paused"},
                                       {"current_stage", repair_json.value("success", false) ? "finalize" : "verification_rerun"},
                                       {"repair_result", repair_json},
                                       {"summary", repair_json.value("summary", Json::object())},
                                       {"stages", Json::array({Json{{"stage", "repair_plan"}, {"status", "succeeded"}},
                                                               Json{{"stage", "context_pack"}, {"status", body.contains("code_context") ? "succeeded" : "optional"}},
                                                               Json{{"stage", "draft_patch"}, {"status", "skipped"}},
                                                               Json{{"stage", "patch_preview"}, {"status", "succeeded"}},
                                                               Json{{"stage", "checkpoint"}, {"status", "succeeded"}},
                                                               Json{{"stage", "patch_apply"}, {"status", "succeeded"}},
                                                               Json{{"stage", "verification_rerun"}, {"status", repair_json.value("success", false) ? "succeeded" : "failed"}},
                                                               Json{{"stage", "finalize"}, {"status", repair_json.value("success", false) ? "succeeded" : "blocked"}}})}};
                }
            }
        }
        auto updated = store.update(workflow_id, final_patch);
        if (!updated.value("success", false)) return updated;
        return Json{{"success", true}, {"workflow", updated["workflow"]}};
    };
    auto start_repair_workflow = svc.start_repair_workflow;
    svc.resume_workflow = [context, start_repair_workflow](const std::string& username,
                                    const std::string& workflow_id,
                                    const Json& body) {
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        auto current = store.get(workflow_id);
        if (!current.value("success", false)) return current;
        auto workflow = current.value("workflow", Json::object());
        auto status = workflow.value("status", "");
        if (!workflow_can_resume(status)) return invalid_workflow_transition("resume", status);
        auto request = workflow.value("request", Json::object());
        if (body.is_object()) {
            for (auto it = body.begin(); it != body.end(); ++it) request[it.key()] = it.value();
        }
        auto workspace_name = workflow.value("workspace", "");
        auto session = workflow.value("session_id", "");
        if (!start_repair_workflow) return Json{{"success", false}, {"error_type", "runtime_workflow_unavailable"}, {"message", "runtime workflow start service unavailable"}};
        return start_repair_workflow(workspace_name, session, username, request);
    };
    svc.cancel_workflow = [context](const std::string& username,
                                    const std::string& workflow_id) {
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        auto current = store.get(workflow_id);
        if (!current.value("success", false)) return current;
        auto workflow = current.value("workflow", Json::object());
        auto status = workflow.value("status", "");
        if (!workflow_can_cancel(status)) return invalid_workflow_transition("cancel", status);
        return store.update(workflow_id, Json{{"status", "cancelled"}, {"current_stage", "cancelled"}});
    };
    svc.workflow_timeline = [context](const std::string& username,
                                      const std::string& workflow_id) {
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        auto current = store.get(workflow_id);
        if (!current.value("success", false)) return current;
        return workflow_timeline_projection(current.value("workflow", Json::object()));
    };
    svc.workflow_integrity = [context](const std::string& username,
                                       const std::string& workflow_id) {
        auto user_dir = context.workspace_resolver.user_dir_for(username);
        audit::RuntimeWorkflowStore store(user_dir / "runtime" / "workflows.jsonl");
        auto current = store.get(workflow_id);
        if (!current.value("success", false)) return current;
        auto report = workflow_integrity_report(current.value("workflow", Json::object()), user_dir);
        report["workflow_id"] = workflow_id;
        return report;
    };
    svc.compact_workflows = [context](const std::string& username) {
        audit::RuntimeWorkflowStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "workflows.jsonl");
        return store.compact();
    };
    return svc;
}

WorkbenchSnapshotApiService make_workbench_snapshot_api_service(ServerCompositionContext context) {
    WorkbenchSnapshotApiService svc;
    svc.snapshot = [context](const std::string& workspace,
                             const std::string& username,
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

        // 堆分配 snapshot — 这个 Json 会累积几十个 context 字段，栈上分配在 Windows 下会溢出
        auto snapshot = std::make_unique<Json>(Json{
            {"success", true},
            {"provider", "workbench"},
            {"workspace", context.workspace_resolver.workspace_or_default(workspace)},
            {"username", username},
            {"index", Json{{"request_scoped", true},
                            {"shared_options", Json{{"max_files", repo_options.max_files},
                                                   {"max_symbols", repo_options.max_symbols},
                                                   {"max_dependencies", repo_options.max_dependencies},
                                                   {"include_external", repo_options.include_external},
                                                   {"include_hidden", repo_options.include_hidden},
                                                   {"refresh", repo_options.refresh}}},
                            {"source_context_lines", context_lines}}}});

        (*snapshot)["overview"] = workbench_result_json(intelligence->overview(repo_options), [](const repo_map::RepoMapOverviewResult& result) {
            return repo_map::to_json(result);
        });

        Json change_context{{"success", true}};
        auto git_status = git::to_json(services.git()->status());
        change_context["git_status"] = git_status;
        change_context["selected_file"] = Json::object();
        change_context["diff"] = Json{{"success", true}, {"diff", ""}, {"staged", false}, {"stat", false}};
        change_context["test_suggestions"] = test_suggestions_from_overview((*snapshot)["overview"]);
        if (!path.empty()) {
            auto selected = selected_git_entry(git_status, path);
            if (!selected.is_null()) change_context["selected_file"] = selected;
            change_context["diff"] = workbench_result_json(services.git()->diff(path, false, false), [](const git::GitDiffResult& result) {
                return git::to_json(result);
            });
        }
        (*snapshot)["change_context"] = change_context;
        (*snapshot)["quality_context"] = quality_context_json(services, request, context_lines, source_max_file_bytes, change_context["test_suggestions"]);
        if (!query_text.empty()) {
            (*snapshot)["files"] = workbench_result_json(intelligence->find_files(query_text, {}, language, limit, repo_options), [](const repo_map::RepoMapFindFilesResult& result) {
                return repo_map::to_json(result);
            });
            (*snapshot)["workspace_symbols"] = workbench_result_json(intelligence->workspace_symbols(query_text, kind, language, limit, code_options), [](const code_intel::CodeIntelWorkspaceSymbolsResult& result) {
                return code_intel::to_json(result);
            });
        }
        if (!path.empty()) {
            (*snapshot)["path"] = workbench_result_json(intelligence->explain_path(path, repo_options), [](const repo_map::RepoMapExplainPathResult& result) {
                return repo_map::to_json(result);
            });
            (*snapshot)["document_symbols"] = workbench_result_json(intelligence->document_symbols(path, code_options), [](const code_intel::CodeIntelDocumentSymbolsResult& result) {
                return code_intel::to_json(result);
            });
            auto focus_line = json_int_or(request, "line", 0);
            (*snapshot)["source_context"] = source_context_json(std::filesystem::path(services.workspace_context().project_path.c_str()),
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
            (*snapshot)["definition"] = workbench_result_json(intelligence->definition(code_query, code_options), [](const code_intel::CodeIntelDefinitionResult& result) {
                return code_intel::to_json(result);
            });
            (*snapshot)["references"] = workbench_result_json(intelligence->references(code_query, code_options), [](const code_intel::CodeIntelReferencesResult& result) {
                return code_intel::to_json(result);
            });
            auto project_root = std::filesystem::path(services.workspace_context().project_path.c_str());
            Json navigation_contexts = Json{{"success", true}, {"definition", Json::object()}, {"references", Json::object()}};
            if (max_location_contexts > 0 && (*snapshot)["definition"].value("success", false)) {
                navigation_contexts["definition"] = source_contexts_from_locations(project_root,
                                                                                      (*snapshot)["definition"].value("definitions", Json::array()),
                                                                                      "definition",
                                                                                      context_lines,
                                                                                      source_max_file_bytes,
                                                                                      max_location_contexts);
            }
            if (max_location_contexts > 0 && (*snapshot)["references"].value("success", false)) {
                navigation_contexts["references"] = source_contexts_from_locations(project_root,
                                                                                      (*snapshot)["references"].value("references", Json::array()),
                                                                                      "reference",
                                                                                      context_lines,
                                                                                      source_max_file_bytes,
                                                                                      max_location_contexts);
            }
            (*snapshot)["navigation_contexts"] = navigation_contexts;
            (*snapshot)["symbol_context"] = symbol_context_json(project_root, *snapshot, context_lines, source_max_file_bytes, max_location_contexts);
            (*snapshot)["dependency_context"] = dependency_context_json(project_root, (*snapshot)["path"], context_lines, source_max_file_bytes, max_location_contexts);
        }
        if (!snapshot->contains("symbol_context")) (*snapshot)["symbol_context"] = Json{{"success", true}, {"document", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}}, {"workspace", Json{{"success", true}, {"contexts", Json::array()}, {"truncated", false}}}, {"summary", Json{{"document_count", 0}, {"workspace_count", 0}}}};
        if (!snapshot->contains("dependency_context")) (*snapshot)["dependency_context"] = Json{{"success", true}, {"dependencies", Json::array()}, {"dependents", Json::array()}, {"related_tests", Json::array()}, {"summary", Json{{"dependency_count", 0}, {"dependent_count", 0}, {"related_test_count", 0}}}};
        if (audit_limit > 0) {
            audit::AuditQuery audit_query;
            audit_query.workspace = context.workspace_resolver.workspace_or_default(workspace);
            audit_query.limit = audit_limit;
            audit::AuditStore store(context.workspace_resolver.user_dir_for(username) / "audit" / "events.jsonl");
            (*snapshot)["audit"] = store.list(audit_query);
        } else {
            (*snapshot)["audit"] = Json{{"success", true}, {"events", Json::array()}, {"truncated", false}};
        }
        (*snapshot)["verification_context"] = verification_context_json(services, *snapshot, request);
        (*snapshot)["failure_context"] = failure_context_json(*snapshot);
        (*snapshot)["impact_context"] = impact_context_json(*snapshot);
        (*snapshot)["readiness_context"] = readiness_context_json(*snapshot);
        (*snapshot)["action_context"] = action_context_json(*snapshot, path);
        (*snapshot)["handoff_context"] = handoff_context_json(*snapshot, path, query_text, symbol);
        (*snapshot)["review_context"] = review_context_json(*snapshot);
        (*snapshot)["gate_context"] = gate_context_json(*snapshot);
        (*snapshot)["timeline_context"] = timeline_context_json(*snapshot);
        (*snapshot)["agent_context"] = agent_context_json(*snapshot);
        (*snapshot)["handoff_package"] = handoff_package_json(*snapshot);
        auto result = std::move(*snapshot);
        return result;
    };
    return svc;
}

CodeIntelApiService make_code_intel_api_service(ServerCompositionContext context) {
    CodeIntelApiService svc;
    svc.capabilities = [context](const std::string& workspace,
                                 const std::string& username) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->capabilities(), [](const code_intel::CodeIntelCapabilitiesResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.document_symbols = [context](const std::string& workspace,
                                     const std::string& username,
                                     std::string_view path) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->document_symbols(path), [](const code_intel::CodeIntelDocumentSymbolsResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.workspace_symbols = [context](const std::string& workspace,
                                      const std::string& username,
                                      std::string_view query,
                                      std::string_view kind,
                                      std::string_view language,
                                      int limit) {
        auto services = application_services(context, workspace, username);
        return app_result_json(services.code_intel()->workspace_symbols(query, kind, language, limit), [](const code_intel::CodeIntelWorkspaceSymbolsResult& result) {
            return code_intel::to_json(result);
        });
    };
    svc.definition = [context](const std::string& workspace,
                               const std::string& username,
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
    svc.references = [context](const std::string& workspace,
                               const std::string& username,
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

    svc.context_pack = [context](const std::string& workspace,
                                 const std::string& username,
                                 const Json& request) {
        auto services = application_services(context, workspace, username);
        auto pack = code_context_pack_json(services, request);
        if (!pack.value("success", false)) return pack;
        pack["context_pack_id"] = workspace::generate_uuid();
        pack["workspace"] = context.workspace_resolver.workspace_or_default(workspace);
        pack["username"] = username;
        if (request.contains("runtime_execution_id")) pack["runtime_execution_id"] = request["runtime_execution_id"];
        auto stored = append_context_pack_file(context.workspace_resolver.user_dir_for(username) / "code_intel" / "context_packs.jsonl", pack);
        return stored.value("success", false) ? Json{{"success", true}, {"context_pack", stored["context_pack"]}} : stored;
    };
    svc.read_context_pack = [context](const std::string& username,
                                      std::string_view context_pack_id) {
        return read_context_pack_file(context.workspace_resolver.user_dir_for(username) / "code_intel" / "context_packs.jsonl", context_pack_id);
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
                        services.runtime,
                        services.workbench);
}

} // namespace ben_gear::server::composition
