#include "capabilities/tool/code_intel_tools.hpp"

#include "intelligence/code_intel/code_intel_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

code_intel::CodeIntelOptions code_intel_options_from_args(const Json& args) {
    code_intel::CodeIntelOptions options;
    options.max_files = args.value("max_files", options.max_files);
    options.max_symbols = args.value("max_symbols", options.max_symbols);
    options.max_file_bytes = args.value("max_file_bytes", options.max_file_bytes);
    options.max_references = args.value("max_references", options.max_references);
    options.include_external = args.value("include_external", options.include_external);
    options.include_hidden = args.value("include_hidden", options.include_hidden);
    return options;
}

code_intel::CodeIntelQuery code_intel_query_from_args(const Json& args) {
    code_intel::CodeIntelQuery query;
    query.path = args.value("path", "");
    query.line = args.value("line", 0);
    query.column = args.value("column", 0);
    query.symbol = args.value("symbol", "");
    query.limit = args.value("limit", 50);
    return query;
}

void register_code_intel_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<code_intel::CodeIntelService> service) {
    if (!service) return;

    registry.register_tool(
        std::string("code_intel_document_symbols"),
        std::string("Return lightweight indexed document symbols for a workspace-relative path. Read-only."),
        {{std::string("path"), {std::string("string"), std::string("Workspace-relative file path"), {}, true}}},
        [service](const Json& args) -> std::string {
            auto result = command_detail::app_result_json(service->document_symbols(args.value("path", "")), [](const code_intel::CodeIntelDocumentSymbolsResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("code_intel_workspace_symbols"),
        std::string("Search lightweight indexed symbols across the workspace. Read-only."),
        {{std::string("query"), {std::string("string"), std::string("Case-insensitive symbol name fragment; empty lists indexed symbols"), {}, false}},
         {std::string("kind"), {std::string("string"), std::string("Optional symbol kind filter, such as class, function, or method"), {}, false}},
         {std::string("language"), {std::string("string"), std::string("Optional language filter, such as cpp or typescript"), {}, false}},
         {std::string("limit"), {std::string("integer"), std::string("Maximum symbols to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto result = command_detail::app_result_json(service->workspace_symbols(args.value("query", ""),
                                                                                      args.value("kind", ""),
                                                                                      args.value("language", ""),
                                                                                      args.value("limit", 50),
                                                                                      code_intel_options_from_args(args)), [](const code_intel::CodeIntelWorkspaceSymbolsResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("code_intel_definition"),
        std::string("Find indexed definition locations by symbol name or file position. Read-only."),
        {{std::string("symbol"), {std::string("string"), std::string("Symbol name to resolve"), {}, false}},
         {std::string("path"), {std::string("string"), std::string("Workspace-relative file path for position-based lookup"), {}, false}},
         {std::string("line"), {std::string("integer"), std::string("1-based line for position-based lookup"), {}, false}},
         {std::string("column"), {std::string("integer"), std::string("1-based column for position-based lookup"), {}, false}},
         {std::string("limit"), {std::string("integer"), std::string("Maximum definitions to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto result = command_detail::app_result_json(service->definition(code_intel_query_from_args(args), code_intel_options_from_args(args)), [](const code_intel::CodeIntelDefinitionResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        std::string("code_intel_references"),
        std::string("Find indexed whole-word reference locations by symbol name or file position. Read-only."),
        {{std::string("symbol"), {std::string("string"), std::string("Symbol name to find references for"), {}, false}},
         {std::string("path"), {std::string("string"), std::string("Workspace-relative file path for position-based lookup"), {}, false}},
         {std::string("line"), {std::string("integer"), std::string("1-based line for position-based lookup"), {}, false}},
         {std::string("column"), {std::string("integer"), std::string("1-based column for position-based lookup"), {}, false}},
         {std::string("limit"), {std::string("integer"), std::string("Maximum references to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto result = command_detail::app_result_json(service->references(code_intel_query_from_args(args), code_intel_options_from_args(args)), [](const code_intel::CodeIntelReferencesResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);
}

} // namespace ben_gear::tools
