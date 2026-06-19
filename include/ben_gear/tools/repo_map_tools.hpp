#pragma once

#include "ben_gear/repo_map/repo_map_service.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

inline repo_map::RepoMapService::Options repo_map_options_from_args(const Json& args) {
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

inline void register_repo_map_tools(llm::ToolRegistry& registry,
                                    std::shared_ptr<repo_map::RepoMapService> service) {
    if (!service) return;

    registry.register_tool(
        base::container::String("repo_map_overview"),
        base::container::String("Return a structured read-only repository overview with important files, symbols, dependencies, git status, and test suggestions."),
        {{base::container::String("refresh"), {base::container::String("boolean"), base::container::String("Force rebuilding the repo map snapshot"), {}, false}},
         {base::container::String("max_files"), {base::container::String("integer"), base::container::String("Maximum files to scan"), {}, false}},
         {base::container::String("max_symbols"), {base::container::String("integer"), base::container::String("Maximum symbols to return"), {}, false}},
         {base::container::String("include_external"), {base::container::String("boolean"), base::container::String("Include external/noisy directories such as third_party and node_modules"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto result = service->overview(repo_map_options_from_args(args)).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("repo_map_find_files"),
        base::container::String("Find indexed repository files by path query, file kind, or language. Read-only."),
        {{base::container::String("query"), {base::container::String("string"), base::container::String("Substring to match against workspace-relative file paths"), {}, false}},
         {base::container::String("kind"), {base::container::String("string"), base::container::String("Optional file kind: source, header, test, config, document, build, generated, external, unknown"), {}, false}},
         {base::container::String("language"), {base::container::String("string"), base::container::String("Optional language filter such as cpp, python, typescript"), {}, false}},
         {base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum results to return, clamped to 200"), {}, false}},
         {base::container::String("refresh"), {base::container::String("boolean"), base::container::String("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto query = args.value("query", "");
            auto kind = args.value("kind", "");
            auto language = args.value("language", "");
            int limit = args.value("limit", 50);
            auto result = service->find_files(query, kind, language, limit, repo_map_options_from_args(args)).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("repo_map_find_symbols"),
        base::container::String("Find lightweight indexed repository symbols by name, kind, or language. Read-only."),
        {{base::container::String("query"), {base::container::String("string"), base::container::String("Substring to match against symbol names"), {}, true}},
         {base::container::String("kind"), {base::container::String("string"), base::container::String("Optional symbol kind: function, method, class, struct, enum, namespace, interface, variable, module, unknown"), {}, false}},
         {base::container::String("language"), {base::container::String("string"), base::container::String("Optional language filter such as cpp, python, typescript"), {}, false}},
         {base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum results to return, clamped to 200"), {}, false}},
         {base::container::String("refresh"), {base::container::String("boolean"), base::container::String("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto query = args.value("query", "");
            auto kind = args.value("kind", "");
            auto language = args.value("language", "");
            int limit = args.value("limit", 50);
            auto result = service->find_symbols(query, kind, language, limit, repo_map_options_from_args(args)).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("repo_map_explain_path"),
        base::container::String("Explain one workspace-relative path using repo map metadata, symbols, dependencies, dependents, and related tests. Read-only."),
        {{base::container::String("path"), {base::container::String("string"), base::container::String("Workspace-relative file path to explain"), {}, true}},
         {base::container::String("refresh"), {base::container::String("boolean"), base::container::String("Force rebuilding the repo map snapshot"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto path = args.value("path", "");
            auto result = service->explain_path(path, repo_map_options_from_args(args)).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);
}

} // namespace ben_gear::tools
