#include "ben_gear/server/composition/server_composition.hpp"

#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/composition/application_services.hpp"

#include <string>
#include <utility>

namespace ben_gear::server::composition {

namespace {

Json app_error_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(std::string(error.details_json.c_str()));
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", std::string(error.code.c_str())},
                {"message", std::string(error.message.c_str())}};
}

template <class T, class Presenter>
Json app_result_json(const domain::AppResult<T>& result, Presenter&& presenter) {
    if (!result.ok()) return app_error_json(result.error());
    return std::forward<Presenter>(presenter)(result.value());
}

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
                        services.audit);
}

} // namespace ben_gear::server::composition
