#include "server/composition/server_composition.hpp"

#include "server/composition/workbench_contexts.hpp"

#include "server/api/handlers.hpp"
#include "server/api/result_presenter.hpp"
#include "server/composition/application_services.hpp"
#include "base/utils/uuid.hpp"

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
        pack["context_pack_id"] = base::utils::generate_uuid();
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
        auto context_lines = std::clamp(json_int_or(request, "context_lines", 8), 0, 50);
        auto source_max_file_bytes = std::clamp(json_int_or(request, "source_max_file_bytes", 256 * 1024), 1024, 2 * 1024 * 1024);
        auto max_location_contexts = std::clamp(json_int_or(request, "max_location_contexts", 8), 0, 50);
        auto path = json_string_or(request, "path");
        auto symbol = json_string_or(request, "symbol");
        auto query_text = json_string_or(request, "query", symbol);
        auto kind = json_string_or(request, "kind");
        auto language = json_string_or(request, "language");

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

        (*snapshot)["change_context"] = Json{{"success", true}};
        (*snapshot)["quality_context"] = Json{{"success", true}};
        (*snapshot)["audit"] = Json{{"success", true}, {"events", Json::array()}, {"truncated", false}};
        (*snapshot)["verification_context"] = Json{{"success", true}, {"read_only", true}, {"commands", Json::array()}, {"detected", Json::object()}, {"diagnostics_provided", false}, {"diagnostic_count", 0}, {"last_run", Json{{"provided", false}}}, {"dirty", false}, {"changed_files", 0}, {"next_steps", Json::array()}};

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

void register_composed_api_routes(Router& router, ApiServices& services) {
    register_api_routes(router,
                        services.session,
                        services.config,
                        services.workspace,
                        services.mcp,
                        services.file,
                        services.repo_map,
                        services.code_intel,
                        services.workbench);
}

} // namespace ben_gear::server::composition
