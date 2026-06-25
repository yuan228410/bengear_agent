#include "ben_gear/server/composition/server_composition.hpp"

#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/result_presenter.hpp"
#include "ben_gear/server/composition/application_services.hpp"
#include "ben_gear/server/composition/command_api_composition.hpp"
#include "ben_gear/audit/audit_store.hpp"

#include <algorithm>
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
                                                             {"refresh", repo_options.refresh}}}}}};

        snapshot["overview"] = workbench_result_json(intelligence->overview(repo_options), [](const repo_map::RepoMapOverviewResult& result) {
            return repo_map::to_json(result);
        });
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
