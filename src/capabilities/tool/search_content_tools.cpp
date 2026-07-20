#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "base/utils/json.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_search_content_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("search_content"),
        std::string("Search files for a literal string (not regex). Returns matching lines with file, line, and column."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Directory to search in")
            }},
            {std::string("query"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Literal text to search (case-sensitive)")
            }},
            {std::string("file_pattern"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Glob pattern for file filtering (default: *)")
            }},
            {std::string("max_results"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Max results (default: 50)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::string query = args.at("query").get<std::string>();
            std::string file_pattern = args.value("file_pattern", "*");
            int max_results = args.value("max_results", 50);
            if (query.empty()) {
                return Json{{"results", Json::array()}, {"error", "query is empty"}}.dump();
            }
            if (!std::filesystem::exists(path)) {
                return Json{{"results", Json::array()}, {"error", "Path not found: " + path}}.dump();
            }

            std::string fp_pre, fp_suf;
            bool fp_wc = false;
            auto star = file_pattern.find('*');
            if (star != std::string::npos) { fp_wc = true; fp_pre = file_pattern.substr(0, star); fp_suf = file_pattern.substr(star + 1); }

            Json results = Json::array();
            int total = 0;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path,
                    std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                if (total >= max_results) break;
                if (file_pattern != "*") {
                    auto fn = entry.path().filename().string();
                    if (fp_wc) {
                        if (fn.size() < fp_pre.size() + fp_suf.size() ||
                            fn.compare(0, fp_pre.size(), fp_pre) != 0 ||
                            fn.compare(fn.size() - fp_suf.size(), fp_suf.size(), fp_suf) != 0) continue;
                    } else if (fn != file_pattern) continue;
                }

                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) continue;
                std::string line;
                int line_num = 0;
                while (std::getline(file, line) && total < max_results) {
                    line_num++;
                    auto col = line.find(query);
                    if (col != std::string::npos) {
                        results.push_back({{"file", entry.path().string()}, {"line", line_num},
                                           {"column", static_cast<int>(col) + 1}, {"content", line}});
                        total++;
                    }
                }
            }
            log::debug_fmt("search_content: query='{}' found={}", query, total);
            return Json{{"results", results}, {"count", total}}.dump();
        }
    );
}

} // namespace ben_gear::tools
