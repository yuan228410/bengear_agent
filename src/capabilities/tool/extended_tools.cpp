#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "base/utils/json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_extended_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("mkdir"),
        std::string("Create a directory. Creates parent directories by default."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Directory path to create")
            }},
            {std::string("parents"), ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string("Create parent directories as needed (default: true)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            bool parents = args.value("parents", true);

            std::error_code ec;
            if (parents) {
                std::filesystem::create_directories(path, ec);
            } else {
                std::filesystem::create_directory(path, ec);
            }
            if (ec) {
                log::error_fmt("mkdir: failed: {} - {}", path, ec.message());
                return Json{{"success", false}, {"error", ec.message()}}.dump();
            }
            log::debug_fmt("mkdir: {}", path);
            return Json{{"success", true}, {"path", path}}.dump();
        }
    );

    registry.register_tool(
        std::string("copy_file"),
        std::string("Copy a file or directory"),
        {
            {std::string("src"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Source path")
            }},
            {std::string("dst"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Destination path")
            }},
            {std::string("recursive"), ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string("Copy directory recursively (default: false)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string src = args.at("src").get<std::string>();
            std::string dst = args.at("dst").get<std::string>();
            bool recursive = args.value("recursive", false);

            std::error_code ec;
            if (std::filesystem::is_directory(src)) {
                if (!recursive) {
                    return Json{{"success", false}, {"error", "Source is a directory. Set recursive=true."}}.dump();
                }
                std::filesystem::copy(src, dst,
                    std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing, ec);
            } else {
                std::filesystem::copy_file(src, dst,
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (ec) {
                log::error_fmt("copy_file: failed: {} -> {} - {}", src, dst, ec.message());
                return Json{{"success", false}, {"error", ec.message()}}.dump();
            }
            log::debug_fmt("copy_file: {} -> {}", src, dst);
            return Json{{"success", true}, {"src", src}, {"dst", dst}}.dump();
        }
    );

    registry.register_tool(
        std::string("file_info"),
        std::string("Get file/directory information: existence, type, size, modification time"),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Path to check")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::filesystem::path p(path);

            if (!std::filesystem::exists(p)) {
                return Json{{"exists", false}, {"path", path}}.dump();
            }

            Json info = {{"exists", true}, {"path", path}};

            std::error_code ec;
            if (std::filesystem::is_directory(p)) {
                info["type"] = "directory";
            } else if (std::filesystem::is_symlink(p)) {
                info["type"] = "symlink";
            } else if (std::filesystem::is_regular_file(p)) {
                info["type"] = "file";
                info["size"] = static_cast<int64_t>(std::filesystem::file_size(p, ec));
                auto mtime = std::filesystem::last_write_time(p, ec);
                if (!ec) {
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        mtime - std::filesystem::file_time_type::clock::now() +
                        std::chrono::system_clock::now());
                    info["modified"] = static_cast<int64_t>(std::chrono::system_clock::to_time_t(sctp));
                }
            } else {
                info["type"] = "other";
            }

            return info.dump();
        }
    );

    registry.register_tool(
        std::string("search_files"),
        std::string("Search for files by name pattern (glob). Returns matching file paths."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Root directory to search from")
            }},
            {std::string("pattern"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Glob pattern to match (e.g. *.cpp, **/*.hpp)")
            }},
            {std::string("recursive"), ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string("Search recursively in subdirectories (default: true)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::string pattern = args.at("pattern").get<std::string>();
            bool recursive = args.value("recursive", true);

            if (!std::filesystem::exists(path)) {
                return std::string(Json{{"matches", Json::array()}, {"count", 0},
                            {"error", "Path does not exist: " + path}}.dump().c_str());
            }

            std::string prefix, suffix;
            bool has_wildcard = false;
            auto star_pos = pattern.find('*');
            if (star_pos != std::string::npos) {
                has_wildcard = true;
                prefix = pattern.substr(0, star_pos);
                suffix = pattern.substr(star_pos + 1);
                if (!suffix.empty() && suffix[0] == '*') {
                    suffix = suffix.substr(1);
                }
            } else {
                prefix = pattern;
            }

            auto match_filename = [&](const std::string& filename) -> bool {
                if (!has_wildcard) return filename == prefix;
                if (filename.size() < prefix.size() + suffix.size()) return false;
                return filename.compare(0, prefix.size(), prefix) == 0 &&
                       filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
            };

            Json matches = Json::array();
            int count = 0;
            const int max_results = 100;
            bool truncated = false;

            std::error_code ec;
            if (recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(path,
                        std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (truncated) { count++; continue; }
                    if (match_filename(entry.path().filename().string())) {
                        if (static_cast<int>(matches.size()) < max_results) {
                            matches.push_back(entry.path().string());
                        } else {
                            truncated = true;
                        }
                        count++;
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
                    if (truncated) { count++; continue; }
                    if (match_filename(entry.path().filename().string())) {
                        if (static_cast<int>(matches.size()) < max_results) {
                            matches.push_back(entry.path().string());
                        } else {
                            truncated = true;
                        }
                        count++;
                    }
                }
            }

            log::debug_fmt("search_files: {} pattern='{}' found={}", path, pattern, count);
            return Json{{"matches", matches}, {"count", count}, {"truncated", truncated}}.dump();
        }
    );

    registry.register_tool(
        std::string("grep_content"),
        std::string("Search file contents by regex pattern. Returns matching lines with file paths and line numbers."),
        {
            {std::string("path"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Root directory to search in")
            }},
            {std::string("pattern"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Regex pattern to search for")
            }},
            {std::string("file_pattern"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Only search files matching this glob (default: *)")
            }},
            {std::string("max_results"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Maximum number of results (default: 50)")
            }}
        },
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::string pattern = args.at("pattern").get<std::string>();
            std::string file_pattern = args.value("file_pattern", "*");
            int max_results = args.value("max_results", 50);

            if (!std::filesystem::exists(path)) {
                return Json{{"results", Json::array()}, {"error", "Path does not exist: " + path}}.dump();
            }

            std::regex re;
            try {
                re = std::regex(pattern);
            } catch (const std::regex_error& e) {
                return Json{{"results", Json::array()}, {"error", "Invalid regex: " + std::string(e.what())}}.dump();
            }

            Json results = Json::array();
            int total = 0;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path,
                    std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                if (total >= max_results) break;

                if (file_pattern != "*") {
                    auto filename = entry.path().filename().string();
                    auto pos = file_pattern.find('*');
                    if (pos != std::string::npos) {
                        std::string fp_prefix = file_pattern.substr(0, pos);
                        std::string fp_suffix = file_pattern.substr(pos + 1);
                        if (filename.size() < fp_prefix.size() + fp_suffix.size() ||
                            filename.substr(0, fp_prefix.size()) != fp_prefix ||
                            filename.substr(filename.size() - fp_suffix.size()) != fp_suffix) {
                            continue;
                        }
                    } else if (filename != file_pattern) {
                        continue;
                    }
                }

                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) continue;

                std::string line;
                int line_num = 0;
                while (std::getline(file, line) && total < max_results) {
                    line_num++;
                    try {
                        if (std::regex_search(line, re)) {
                            results.push_back({
                                {"file", entry.path().string()},
                                {"line", line_num},
                                {"content", line}
                            });
                            total++;
                        }
                    } catch (const std::regex_error&) { break; }
                }
            }

            log::debug_fmt("grep_content: {} pattern='{}' found={}", path, pattern, total);
            return Json{{"results", results}, {"count", total}}.dump();
        }
    );
}

} // namespace ben_gear::tools
