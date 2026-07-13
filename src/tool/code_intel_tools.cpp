#include "tool/code_intel_tools.hpp"

#include "intelligence/code_intel/code_intel_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

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
        base::container::String("code_intel_document_symbols"),
        base::container::String("Return lightweight indexed document symbols for a workspace-relative path. Read-only."),
        {{base::container::String("path"), {base::container::String("string"), base::container::String("Workspace-relative file path"), {}, true}}},
        [service](const Json& args) -> base::container::String {
            auto result = command_detail::app_result_json(service->document_symbols(args.value("path", "")), [](const code_intel::CodeIntelDocumentSymbolsResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        base::container::String("code_intel_workspace_symbols"),
        base::container::String("Search lightweight indexed symbols across the workspace. Read-only."),
        {{base::container::String("query"), {base::container::String("string"), base::container::String("Case-insensitive symbol name fragment; empty lists indexed symbols"), {}, false}},
         {base::container::String("kind"), {base::container::String("string"), base::container::String("Optional symbol kind filter, such as class, function, or method"), {}, false}},
         {base::container::String("language"), {base::container::String("string"), base::container::String("Optional language filter, such as cpp or typescript"), {}, false}},
         {base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum symbols to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> base::container::String {
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
        base::container::String("code_intel_definition"),
        base::container::String("Find indexed definition locations by symbol name or file position. Read-only."),
        {{base::container::String("symbol"), {base::container::String("string"), base::container::String("Symbol name to resolve"), {}, false}},
         {base::container::String("path"), {base::container::String("string"), base::container::String("Workspace-relative file path for position-based lookup"), {}, false}},
         {base::container::String("line"), {base::container::String("integer"), base::container::String("1-based line for position-based lookup"), {}, false}},
         {base::container::String("column"), {base::container::String("integer"), base::container::String("1-based column for position-based lookup"), {}, false}},
         {base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum definitions to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto result = command_detail::app_result_json(service->definition(code_intel_query_from_args(args), code_intel_options_from_args(args)), [](const code_intel::CodeIntelDefinitionResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);

    registry.register_tool(
        base::container::String("code_intel_references"),
        base::container::String("Find indexed whole-word reference locations by symbol name or file position. Read-only."),
        {{base::container::String("symbol"), {base::container::String("string"), base::container::String("Symbol name to find references for"), {}, false}},
         {base::container::String("path"), {base::container::String("string"), base::container::String("Workspace-relative file path for position-based lookup"), {}, false}},
         {base::container::String("line"), {base::container::String("integer"), base::container::String("1-based line for position-based lookup"), {}, false}},
         {base::container::String("column"), {base::container::String("integer"), base::container::String("1-based column for position-based lookup"), {}, false}},
         {base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum references to return, clamped to 200"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto result = command_detail::app_result_json(service->references(code_intel_query_from_args(args), code_intel_options_from_args(args)), [](const code_intel::CodeIntelReferencesResult& value) {
                return code_intel::to_json(value);
            });
            return command_detail::json_tool_output(result);
        },
        true);
}

} // namespace ben_gear::tools
