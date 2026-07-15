#include "capabilities/tool/repo_map_tools.hpp"

#include "intelligence/repo_map/repo_map_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

repo_map::RepoMapService::Options repo_map_options_from_args(const Json& args) {
    repo_map::RepoMapService::Options options;
    options.max_files = args.value("max_files", options.max_files);
    options.max_symbols = args.value("max_symbols", options.max_symbols);
    options.max_dependencies = args.value("max_dependencies", options.max_dependencies);
    options.max_file_bytes = args.value("max_file_bytes", options.max_file_bytes);
    options.include_external = args.value("include_external", options.include_external);
    options.include_hidden = args.value("include_hidden", options.include_hidden);
    options.refresh = args.value("refresh", options.refresh);
    return options;
}

void register_repo_map_tools(llm::ToolRegistry& registry,
                                    std::shared_ptr<repo_map::RepoMapService> service) {
    if (!service) return;

    registry.register_tool(
        std::string("repo_map_overview"),
        std::string("Return a structured read-only repository overview with important files, symbols, dependencies, git status, and test suggestions."),
        {{std::string("refresh"), {std::string("boolean"), std::string("Force rebuilding the repo map snapshot"), {}, false}},
         {std::string("max_files"), {std::string("integer"), std::string("Maximum files to scan"), {}, false}},
         {std::string("max_symbols"), {std::string("integer"), std::string("Maximum symbols to return"), {}, false}},
         {std::string("include_external"), {std::string("boolean"), std::string("Include external/noisy directories such as third_party and node_modules"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto result = command_detail::app_result_json(service->overview(repo_map_options_from_args(args)), [](const repo_map::RepoMapOverviewResult& value) {
                return repo_map::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("repo_map_find_files"),
        std::string("Find indexed repository files by path query, file kind, or language. Read-only."),
        {{std::string("query"), {std::string("string"), std::string("Substring to match against workspace-relative file paths"), {}, false}},
         {std::string("kind"), {std::string("string"), std::string("Optional file kind: source, header, test, config, document, build, generated, external, unknown"), {}, false}},
         {std::string("language"), {std::string("string"), std::string("Optional language filter such as cpp, python, typescript"), {}, false}},
         {std::string("limit"), {std::string("integer"), std::string("Maximum results to return, clamped to 200"), {}, false}},
         {std::string("refresh"), {std::string("boolean"), std::string("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto query = args.value("query", "");
            auto kind = args.value("kind", "");
            auto language = args.value("language", "");
            int limit = args.value("limit", 50);
            auto result = command_detail::app_result_json(service->find_files(query, kind, language, limit, repo_map_options_from_args(args)), [](const repo_map::RepoMapFindFilesResult& value) {
                return repo_map::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("repo_map_find_symbols"),
        std::string("Find lightweight indexed repository symbols by name, kind, or language. Read-only."),
        {{std::string("query"), {std::string("string"), std::string("Substring to match against symbol names"), {}, true}},
         {std::string("kind"), {std::string("string"), std::string("Optional symbol kind: function, method, class, struct, enum, namespace, interface, variable, module, unknown"), {}, false}},
         {std::string("language"), {std::string("string"), std::string("Optional language filter such as cpp, python, typescript"), {}, false}},
         {std::string("limit"), {std::string("integer"), std::string("Maximum results to return, clamped to 200"), {}, false}},
         {std::string("refresh"), {std::string("boolean"), std::string("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto query = args.value("query", "");
            auto kind = args.value("kind", "");
            auto language = args.value("language", "");
            int limit = args.value("limit", 50);
            auto result = command_detail::app_result_json(service->find_symbols(query, kind, language, limit, repo_map_options_from_args(args)), [](const repo_map::RepoMapFindSymbolsResult& value) {
                return repo_map::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("repo_map_explain_path"),
        std::string("Explain one workspace-relative path using repo map metadata, symbols, dependencies, dependents, and related tests. Read-only."),
        {{std::string("path"), {std::string("string"), std::string("Workspace-relative file path to explain"), {}, true}},
         {std::string("refresh"), {std::string("boolean"), std::string("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto path = args.value("path", "");
            auto result = command_detail::app_result_json(service->explain_path(path, repo_map_options_from_args(args)), [](const repo_map::RepoMapExplainPathResult& value) {
                return repo_map::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);
}

} // namespace ben_gear::tools
