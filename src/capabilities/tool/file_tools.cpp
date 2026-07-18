#include "capabilities/tool/builtin_tools.hpp"

#include "base/log/logger.hpp"
#include "base/utils/json.hpp"

#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_file_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("read_file"),
        std::string("Read file content. Supports text files with UTF-8 encoding."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("File path to read")
            }},
            {std::string("start_line"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Start line number (1-based, optional)")
            }},
            {std::string("end_line"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("End line number (inclusive, optional)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();

            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                log::error_fmt("read_file: cannot open: {}", path);
                return ("Error: Cannot open file: " + path);
            }

            auto size = file.tellg();
            file.seekg(0, std::ios::beg);

            if (size < 0) {
                log::error_fmt("read_file: cannot determine file size: {}", path);
                return ("Error: Cannot determine file size: " + path);
            }

            if (args.contains("start_line") || args.contains("end_line")) {
                int start = args.value("start_line", 1);
                int end = args.value("end_line", INT_MAX);

                std::string result;
                static constexpr auto kMaxReserve = static_cast<size_t>(100 * 1024 * 1024);
                result.reserve(size > 0 ? std::min(static_cast<size_t>(size), kMaxReserve) : 4096);
                std::string line;
                int line_num = 1;
                while (std::getline(file, line)) {
                    if (line_num >= start && line_num <= end) {
                        result += std::to_string(line_num) + "|" + line + "\n";
                    }
                    if (line_num > end) break;
                    line_num++;
                }
                return std::string(result.data(), result.size());
            }

            std::string content;
            if (size > 0) {
                static const auto kMaxFileSize = static_cast<std::streampos>(100 * 1024 * 1024);  // 100MB
                if (size > kMaxFileSize) {
                    log::error_fmt("read_file: file too large: {} ({} bytes)", path, size);
                    return ("Error: File too large: " + path + " (" + std::to_string(size) + " bytes)");
                }
                content.resize(static_cast<size_t>(size));
            }
            file.read(content.data(), size);
            auto actual = file.gcount();
            content.resize(static_cast<size_t>(actual));

            log::debug_fmt("read_file: {} ({} bytes)", path, actual);
            return std::string(content.data(), content.size());
        }
    );

    registry.register_tool(
        std::string("write_file"),
        std::string("Write content to a file. Supports overwrite, append, and line-range replacement. "
            "Use start_line/end_line to replace specific lines (1-based, inclusive). "
            "Example: start_line=5, end_line=10 replaces lines 5-10 with content."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("File path to write")
            }},
            {std::string("content"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Content to write")
            }},
            {std::string("mode"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Write mode: 'overwrite' (default), 'append', or 'replace'")
            }},
            {std::string("start_line"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Start line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }},
            {std::string("end_line"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("End line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::string content = args.at("content").get<std::string>();
            std::string mode = args.value("mode", "overwrite");

            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

            if (mode == "replace") {
                int start_line = args.value("start_line", 0);
                int end_line = args.value("end_line", 0);

                if (start_line <= 0 || end_line <= 0 || start_line > end_line) {
                    log::error_fmt("write_file replace: invalid line range start={} end={}", start_line, end_line);
                    return std::string("Error: Invalid line range for replace mode");
                }

                std::ifstream in_file(path);
                if (!in_file) {
                    log::error_fmt("write_file replace: cannot open for reading: {}", path);
                    return ("Error: Cannot open file for reading: " + path);
                }

                std::vector<std::string> lines;
                std::string line;
                auto est_size = std::filesystem::file_size(path, ec);
                if (!ec && est_size > 0) {
                    lines.reserve(static_cast<size_t>(est_size / 40) + 1);
                }
                while (std::getline(in_file, line)) {
                    lines.push_back(line);
                }
                in_file.close();

                std::vector<std::string> new_lines;
                std::istringstream content_stream(content);
                std::string content_line;
                while (std::getline(content_stream, content_line)) {
                    new_lines.push_back(content_line);
                }

                int total_lines = static_cast<int>(lines.size());
                if (start_line > total_lines) {
                    for (auto& nl : new_lines) {
                        lines.push_back(std::move(nl));
                    }
                } else {
                    int replace_end = std::min(end_line, total_lines);
                    lines.erase(lines.begin() + start_line - 1, lines.begin() + replace_end);
                    lines.insert(lines.begin() + start_line - 1,
                                 std::make_move_iterator(new_lines.begin()),
                                 std::make_move_iterator(new_lines.end()));
                }

                std::ofstream out_file(path, std::ios::trunc);
                if (!out_file) {
                    log::error_fmt("write_file replace: cannot open for writing: {}", path);
                    return ("Error: Cannot open file for writing: " + path);
                }

                for (size_t i = 0; i < lines.size(); ++i) {
                    out_file << lines[i];
                    if (i + 1 < lines.size()) out_file << '\n';
                }

                log::debug_fmt("write_file replace: {} (replaced lines {}-{}, {} new lines)",
                               path, start_line, end_line, (int)new_lines.size());
                return std::string(("Success: Replaced lines " + std::to_string(start_line) + "-"
                                          + std::to_string(end_line) + " in " + path).c_str());
            }

            std::ofstream file;
            if (mode == "append") {
                file.open(path, std::ios::app);
            } else {
                file.open(path, std::ios::trunc);
            }

            if (!file) {
                log::error_fmt("write_file: cannot open for writing: {}", path);
                return ("Error: Cannot open file for writing: " + path);
            }

            file << content;
            log::debug_fmt("write_file: {} ({} bytes, mode={})", path, content.size(), mode);
            return ("Success: Written to " + path);
        }
    );

    registry.register_tool(
        std::string("delete_file"),
        std::string("Delete a file or empty directory"),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("File or directory path to delete")
            }},
            {std::string("recursive"), ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string("Recursively delete non-empty directory (default: false)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            bool recursive = args.value("recursive", false);

            std::error_code ec;
            if (recursive) {
                std::filesystem::remove_all(path, ec);
            } else {
                std::filesystem::remove(path, ec);
            }

            if (ec) {
                log::error_fmt("delete_file: failed: {} - {}", path, ec.message());
                return ("Error: " + ec.message());
            }
            log::debug_fmt("delete_file: {}", path);
            return ("Success: Deleted " + path);
        }
    );

    registry.register_tool(
        std::string("list_directory"),
        std::string("List contents of a directory"),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Directory path to list")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();

            if (!std::filesystem::exists(path)) {
                return ("Error: Directory does not exist: " + path);
            }

            std::string result;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                result += entry.path().filename().string();
                if (entry.is_directory()) result += "/";
                result += "\n";
            }

            return result.empty() ? std::string("Empty directory") : result;
        }
    );

    registry.register_tool(
        std::string("rename_file"),
        std::string("Rename or move a file/directory"),
        {
            {std::string("src"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Source path")
            }},
            {std::string("dst"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Destination path")
            }}
        },
        [](const Json& args) -> std::string {
            std::string src = args.at("src").get<std::string>();
            std::string dst = args.at("dst").get<std::string>();

            std::error_code ec;
            std::filesystem::rename(src, dst, ec);

            if (ec) {
                log::error_fmt("rename_file: failed: {} -> {} - {}", src, dst, ec.message());
                return ("Error: " + ec.message());
            }
            log::debug_fmt("rename_file: {} -> {}", src, dst);
            return ("Success: Renamed " + src + " to " + dst);
        }
    );
}

} // namespace ben_gear::tools
