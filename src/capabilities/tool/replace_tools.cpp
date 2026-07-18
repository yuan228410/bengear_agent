#include "capabilities/tool/builtin_tools.hpp"

#include "base/log/logger.hpp"
#include "base/utils/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_replace_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("replace_in_file"),
        std::string("Replace exact text in a file. First match of old is replaced with new. "
            "Include 2-3 lines of surrounding context in old for uniqueness. "
            "If exact match fails, falls back to whitespace-normalized matching."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("File path to edit")
            }},
            {std::string("old"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Exact text to replace (must be unique in file)")
            }},
            {std::string("new"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Replacement text")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::string old_str = args.at("old").get<std::string>();
            std::string new_str = args.at("new").get<std::string>();

            std::error_code ec;
            auto fsize = std::filesystem::file_size(path, ec);
            if (ec) {
                return Json{{"success", false}, {"error", "Cannot read: " + path}}.dump();
            }

            std::string content(static_cast<size_t>(fsize), '\0');
            {
                std::ifstream in_file(path, std::ios::binary);
                in_file.read(content.data(), static_cast<std::streamsize>(content.size()));
                content.resize(static_cast<size_t>(in_file.gcount()));
            }

            size_t pos = content.find(old_str);
            bool used_fuzzy = false;

            if (pos == std::string::npos) {
                // 精确匹配失败 → 用 old_str 首行（去空白）定位
                auto first_nl = old_str.find('\n');
                std::string first_line = first_nl != std::string::npos
                    ? std::string(old_str.data(), first_nl) : std::string(old_str);
                // 去首尾空白
                while (!first_line.empty() && (first_line.front() == ' ' || first_line.front() == '\t'))
                    first_line.erase(0, 1);
                while (!first_line.empty() && (first_line.back() == ' ' || first_line.back() == '\t' || first_line.back() == '\r'))
                    first_line.pop_back();

                if (first_line.empty()) {
                    return std::string(Json{{"success", false},
                        {"error", "old_string not found in file"}}.dump().c_str());
                }

                // 在 content 中逐行匹配首行
                size_t search_from = 0;
                while (search_from < content.size()) {
                    auto nl = content.find('\n', search_from);
                    size_t line_end = nl != std::string::npos ? nl : content.size();
                    // 规范化当前行
                    size_t line_start = search_from;
                    while (line_start < line_end && (content[line_start] == ' ' || content[line_start] == '\t'))
                        line_start++;
                    size_t trimmed_end = line_end;
                    while (trimmed_end > line_start && (content[trimmed_end - 1] == ' ' || content[trimmed_end - 1] == '\t' || content[trimmed_end - 1] == '\r'))
                        trimmed_end--;

                    if (trimmed_end - line_start == first_line.size() &&
                        std::memcmp(content.data() + line_start, first_line.data(), first_line.size()) == 0) {
                        // 首行匹配 — 检查 old_str 是否在原内容中
                        pos = content.find(old_str, line_start > old_str.size() ? line_start - old_str.size() : 0);
                        if (pos == std::string::npos) {
                            // 宽松匹配：如果首行匹配但精确搜索失败，就用首行位置
                            pos = line_start;
                        }
                        used_fuzzy = true;
                        break;
                    }
                    if (nl == std::string::npos) break;
                    search_from = nl + 1;
                }

                if (!used_fuzzy) {
                    return std::string(Json{{"success", false},
                        {"error", "old_string not found in file"}}.dump().c_str());
                }
            }
            if (content.find(old_str, pos + old_str.size()) != std::string::npos) {
                return std::string(Json{{"success", false},
                    {"error", "old_string matches multiple locations. Add more context to make it unique."}}.dump().c_str());
            }

            content.replace(pos, old_str.size(), new_str);

            std::filesystem::copy_file(path, path + ".bak",
                std::filesystem::copy_options::overwrite_existing, ec);

            std::ofstream out_file(path, std::ios::binary | std::ios::trunc);
            if (!out_file) {
                return std::string(Json{{"success", false},
                    {"error", "Cannot write: " + path}}.dump().c_str());
            }
            out_file.write(content.data(), static_cast<std::streamsize>(content.size()));

            int old_lines = static_cast<int>(std::count(old_str.begin(), old_str.end(), '\n')) + 1;
            int new_lines = static_cast<int>(std::count(new_str.begin(), new_str.end(), '\n')) + 1;
            auto summary = std::string(used_fuzzy ? "(fuzzy match) " : "")
                         + "Replaced " + std::to_string(old_lines) + " line(s) with "
                         + std::to_string(new_lines);
            log::info_fmt("replace_in_file: {} (backup: {}.bak)", path, path);
            return Json{{"success", true}, {"summary", summary}}.dump();
        }
    );
}

} // namespace ben_gear::tools
