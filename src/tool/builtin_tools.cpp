#include "tool/builtin_tools.hpp"

#include "base/log/logger.hpp"
#include "base/platform/os.hpp"

#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if BEN_GEAR_PLATFORM_POSIX
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace ben_gear::tools {

using namespace ben_gear::llm;

// ════════════════════════════════════════════════════════════════════
//  register_file_tools
// ════════════════════════════════════════════════════════════════════

void register_file_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("read_file"),
        container::String("Read file content. Supports text files with UTF-8 encoding."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("File path to read")
            }},
            {container::String("start_line"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Start line number (1-based, optional)")
            }},
            {container::String("end_line"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("End line number (inclusive, optional)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();

            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                log::error_fmt("read_file: cannot open: {}", path);
                return container::String(("Error: Cannot open file: " + path).c_str());
            }

            auto size = file.tellg();
            file.seekg(0, std::ios::beg);

            if (args.contains("start_line") || args.contains("end_line")) {
                int start = args.value("start_line", 1);
                int end = args.value("end_line", INT_MAX);

                std::string result;
                result.reserve(size > 0 ? static_cast<size_t>(size) : 4096);
                std::string line;
                int line_num = 1;
                while (std::getline(file, line)) {
                    if (line_num >= start && line_num <= end) {
                        result += std::to_string(line_num) + "|" + line + "\n";
                    }
                    if (line_num > end) break;
                    line_num++;
                }
                return container::String(result.data(), result.size());
            }

            std::string content;
            if (size > 0) content.resize(static_cast<size_t>(size));
            file.read(content.data(), size);
            auto actual = file.gcount();
            content.resize(static_cast<size_t>(actual));

            log::debug_fmt("read_file: {} ({} bytes)", path, actual);
            return container::String(content.data(), content.size());
        }
    );

    registry.register_tool(
        container::String("write_file"),
        container::String("Write content to a file. Supports overwrite, append, and line-range replacement. "
            "Use start_line/end_line to replace specific lines (1-based, inclusive). "
            "Example: start_line=5, end_line=10 replaces lines 5-10 with content."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("File path to write")
            }},
            {container::String("content"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Content to write")
            }},
            {container::String("mode"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Write mode: 'overwrite' (default), 'append', or 'replace'")
            }},
            {container::String("start_line"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Start line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }},
            {container::String("end_line"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("End line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }}
        },
        [](const Json& args) -> container::String {
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
                    return container::String("Error: Invalid line range for replace mode");
                }

                std::ifstream in_file(path);
                if (!in_file) {
                    log::error_fmt("write_file replace: cannot open for reading: {}", path);
                    return container::String(("Error: Cannot open file for reading: " + path).c_str());
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
                    return container::String(("Error: Cannot open file for writing: " + path).c_str());
                }

                for (size_t i = 0; i < lines.size(); ++i) {
                    out_file << lines[i];
                    if (i + 1 < lines.size()) out_file << '\n';
                }

                log::debug_fmt("write_file replace: {} (replaced lines {}-{}, {} new lines)",
                               path, start_line, end_line, (int)new_lines.size());
                return container::String(("Success: Replaced lines " + std::to_string(start_line) + "-"
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
                return container::String(("Error: Cannot open file for writing: " + path).c_str());
            }

            file << content;
            log::debug_fmt("write_file: {} ({} bytes, mode={})", path, content.size(), mode);
            return container::String(("Success: Written to " + path).c_str());
        }
    );

    registry.register_tool(
        container::String("delete_file"),
        container::String("Delete a file or empty directory"),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("File or directory path to delete")
            }},
            {container::String("recursive"), ToolParameterSchema{
                .type = container::String("boolean"),
                .description = container::String("Recursively delete non-empty directory (default: false)")
            }}
        },
        [](const Json& args) -> container::String {
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
                return container::String(("Error: " + ec.message()).c_str());
            }
            log::debug_fmt("delete_file: {}", path);
            return container::String(("Success: Deleted " + path).c_str());
        }
    );

    registry.register_tool(
        container::String("list_directory"),
        container::String("List contents of a directory"),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Directory path to list")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();

            if (!std::filesystem::exists(path)) {
                return container::String(("Error: Directory does not exist: " + path).c_str());
            }

            std::string result;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                result += entry.path().filename().string();
                if (entry.is_directory()) result += "/";
                result += "\n";
            }

            return result.empty() ? container::String("Empty directory") : container::String(result.c_str());
        }
    );

    registry.register_tool(
        container::String("rename_file"),
        container::String("Rename or move a file/directory"),
        {
            {container::String("src"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Source path")
            }},
            {container::String("dst"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Destination path")
            }}
        },
        [](const Json& args) -> container::String {
            std::string src = args.at("src").get<std::string>();
            std::string dst = args.at("dst").get<std::string>();

            std::error_code ec;
            std::filesystem::rename(src, dst, ec);

            if (ec) {
                log::error_fmt("rename_file: failed: {} -> {} - {}", src, dst, ec.message());
                return container::String(("Error: " + ec.message()).c_str());
            }
            log::debug_fmt("rename_file: {} -> {}", src, dst);
            return container::String(("Success: Renamed " + src + " to " + dst).c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_shell_tools
// ════════════════════════════════════════════════════════════════════

void register_shell_tools(ToolRegistry& registry, int default_timeout) {
    registry.register_tool(
        container::String("execute_command"),
        container::String("Execute a shell command and return the output with exit code"),
        {
            {container::String("command"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Shell command to execute")
            }},
            {container::String("timeout"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Timeout in seconds (default: 30)")
            }},
            {container::String("cwd"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Working directory for the command (optional)")
            }}
        },
        [default_timeout](const Json& args) -> container::String {
            std::string command = args.at("command").get<std::string>();
            int timeout = args.value("timeout", default_timeout);
            std::string cwd = args.value("cwd", "");

            log::info_fmt("execute_command: {} (cwd={} timeout={}s)", command, cwd.empty() ? "." : cwd, timeout);
            auto start = std::chrono::steady_clock::now();
            int exit_code = -1;
            bool timed_out = false;
            std::string result;

#if BEN_GEAR_PLATFORM_WINDOWS
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE read_end = nullptr, write_end = nullptr;
            if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
                log::error_fmt("execute_command: CreatePipe failed");
                return container::String(Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump().c_str());
            }
            SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.hStdError = write_end;
            si.hStdOutput = write_end;
            si.dwFlags |= STARTF_USESTDHANDLES;
            PROCESS_INFORMATION pi{};

            std::string cmd_line = command + " 2>&1";
            if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, TRUE,
                                CREATE_NEW_PROCESS_GROUP, nullptr,
                                cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
                CloseHandle(read_end);
                CloseHandle(write_end);
                log::error_fmt("execute_command: CreateProcess failed");
                return container::String(Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump().c_str());
            }
            CloseHandle(write_end);
            CloseHandle(pi.hThread);

            char buffer[4096];
            DWORD bytes_read = 0;
            for (;;) {
                DWORD avail = 0;
                if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
                    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
                    Sleep(10);
                    continue;
                }
                if (!ReadFile(read_end, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0) break;
                result.append(buffer, bytes_read);
            }
            for (;;) {
                DWORD avail = 0;
                if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
                if (!ReadFile(read_end, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0) break;
                result.append(buffer, bytes_read);
            }
            CloseHandle(read_end);

            DWORD wait_result = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout) * 1000);
            if (wait_result == WAIT_TIMEOUT) {
                TerminateProcess(pi.hProcess, 1);
                timed_out = true;
                exit_code = -1;
            } else {
                DWORD code = 0;
                GetExitCodeProcess(pi.hProcess, &code);
                exit_code = static_cast<int>(code);
            }
            CloseHandle(pi.hProcess);
#else
            int pipefd[2];
            if (pipe(pipefd) != 0) {
                log::error_fmt("execute_command: pipe failed");
                return container::String(Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump().c_str());
            }

            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                log::error_fmt("execute_command: fork failed");
                return container::String(Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump().c_str());
            }

            if (pid == 0) {
                setpgid(0, 0);
                if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
                    _exit(126);
                }
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                _exit(127);
            }

            close(pipefd[1]);

            std::string read_result;
            std::atomic<bool> read_done{false};
            std::thread reader([&] {
                char buffer[4096];
                ssize_t n;
                while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
                    read_result.append(buffer, static_cast<size_t>(n));
                }
                read_done.store(true, std::memory_order_release);
            });

            int status = 0;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
            while (std::chrono::steady_clock::now() < deadline) {
                if (read_done.load(std::memory_order_acquire)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (!read_done.load(std::memory_order_acquire)) {
                kill(-pid, SIGKILL);
                close(pipefd[0]);
                reader.join();
                waitpid(pid, &status, 0);
                timed_out = true;
                exit_code = -1;
                result = std::move(read_result);
                goto build_result;
            }

            close(pipefd[0]);
            reader.join();
            result = std::move(read_result);

            waitpid(pid, &status, 0);
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

        build_result:
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }

            if (timed_out) {
                log::error_fmt("execute_command: timed_out after {}ms", elapsed_ms);
                return container::String(Json{{"stdout", result}, {"exit_code", -1}, {"success", false}, {"timed_out", true}}.dump().c_str());
            }

            bool success = (exit_code == 0);
            if (!success) {
                log::error_fmt("execute_command: exit_code={} elapsed={}ms output_len={}", exit_code, elapsed_ms, result.size());
            } else {
                log::info_fmt("execute_command: exit_code=0 elapsed={}ms output_len={}", elapsed_ms, result.size());
            }

            return container::String(Json{{"stdout", result}, {"exit_code", exit_code}, {"success", success}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_http_tools
// ════════════════════════════════════════════════════════════════════

void register_http_tools(ToolRegistry& registry, net::IoContext& io_ctx) {
    registry.register_tool(
        container::String("http_get"),
        container::String("Perform an HTTP GET request and return the response"),
        {
            {container::String("url"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("URL to fetch")
            }},
            {container::String("headers"), ToolParameterSchema{
                .type = container::String("array"),
                .description = container::String("Optional HTTP headers (array of 'Key: Value' strings)")
            }}
        },
        [&io_ctx](const Json& args) -> container::String {
            std::string url = args.at("url").get<std::string>();
            std::vector<std::string> headers;
            if (args.contains("headers") && args.at("headers").is_array()) {
                for (const auto& h : args.at("headers")) {
                    headers.push_back(h.get<std::string>());
                }
            }
            constexpr int max_retries = 2;
            for (int attempt = 0; attempt <= max_retries; ++attempt) {
                try {
                    net::HttpClient client;
                    auto response = net::sync_wait(io_ctx.loop(),
                        client.get_async(io_ctx.loop(), url, headers));
                    log::debug_fmt("http_get: {} -> status={}", url, response.status);
                    if (response.status == 0) {
                        if (attempt < max_retries) {
                            log::warn_fmt("http_get retry {}/{}: {} - no response", attempt + 1, max_retries, url);
                            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                            continue;
                        }
                        return container::String(Json{{"success", false}, {"status", 0}, {"error", "connection failed after retries"}}.dump().c_str());
                    }
                    return container::String(Json{{"success", true}, {"status", response.status}, {"body", response.body}}.dump().c_str());
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    bool transient = err.find("TLS handshake") != std::string::npos ||
                                     err.find("DecryptMessage") != std::string::npos ||
                                     err.find("reset") != std::string::npos ||
                                     err.find("timeout") != std::string::npos ||
                                     err.find("refused") != std::string::npos;
                    if (transient && attempt < max_retries) {
                        log::warn_fmt("http_get retry {}/{}: {} - {}", attempt + 1, max_retries, url, err);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                        continue;
                    }
                    log::error_fmt("http_get failed: {} - {}", url, err);
                    return container::String(Json{{"success", false}, {"error", err}}.dump().c_str());
                }
            }
            return container::String(Json{{"success", false}, {"error", "unreachable"}}.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("http_post"),
        container::String("Perform an HTTP POST request with JSON body"),
        {
            {container::String("url"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("URL to post to")
            }},
            {container::String("body"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("JSON request body")
            }},
            {container::String("headers"), ToolParameterSchema{
                .type = container::String("array"),
                .description = container::String("Optional HTTP headers (array of 'Key: Value' strings)")
            }}
        },
        [&io_ctx](const Json& args) -> container::String {
            std::string url = args.at("url").get<std::string>();
            std::string body = args.at("body").get<std::string>();
            std::vector<std::string> headers;
            if (args.contains("headers") && args.at("headers").is_array()) {
                for (const auto& h : args.at("headers")) {
                    headers.push_back(h.get<std::string>());
                }
            }
            constexpr int max_retries = 2;
            for (int attempt = 0; attempt <= max_retries; ++attempt) {
                try {
                    net::HttpClient client;
                    container::Vector<container::String> c_headers;
                    for (const auto& h : headers) {
                        c_headers.push_back(container::String(h.data(), h.size()));
                    }
                    auto response = net::sync_wait(io_ctx.loop(),
                        client.post_json_async(io_ctx.loop(),
                            container::String(url.data(), url.size()),
                            container::String(body.data(), body.size()),
                            std::move(c_headers)));
                    log::debug_fmt("http_post: {} -> status={}", url, response.status);
                    if (response.status == 0) {
                        if (attempt < max_retries) {
                            log::warn_fmt("http_post retry {}/{}: {} - no response", attempt + 1, max_retries, url);
                            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                            continue;
                        }
                        return container::String(Json{{"success", false}, {"status", 0}, {"error", "connection failed after retries"}}.dump().c_str());
                    }
                    return container::String(Json{{"success", true}, {"status", response.status}, {"body", response.body}}.dump().c_str());
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    bool transient = err.find("TLS handshake") != std::string::npos ||
                                     err.find("DecryptMessage") != std::string::npos ||
                                     err.find("reset") != std::string::npos ||
                                     err.find("timeout") != std::string::npos ||
                                     err.find("refused") != std::string::npos;
                    if (transient && attempt < max_retries) {
                        log::warn_fmt("http_post retry {}/{}: {} - {}", attempt + 1, max_retries, url, err);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                        continue;
                    }
                    log::error_fmt("http_post failed: {} - {}", url, err);
                    return container::String(Json{{"success", false}, {"error", err}}.dump().c_str());
                }
            }
            return container::String(Json{{"success", false}, {"error", "unreachable"}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_extended_tools
// ════════════════════════════════════════════════════════════════════

void register_extended_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("mkdir"),
        container::String("Create a directory. Creates parent directories by default."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Directory path to create")
            }},
            {container::String("parents"), ToolParameterSchema{
                .type = container::String("boolean"),
                .description = container::String("Create parent directories as needed (default: true)")
            }}
        },
        [](const Json& args) -> container::String {
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
                return container::String(Json{{"success", false}, {"error", ec.message()}}.dump().c_str());
            }
            log::debug_fmt("mkdir: {}", path);
            return container::String(Json{{"success", true}, {"path", path}}.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("copy_file"),
        container::String("Copy a file or directory"),
        {
            {container::String("src"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Source path")
            }},
            {container::String("dst"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Destination path")
            }},
            {container::String("recursive"), ToolParameterSchema{
                .type = container::String("boolean"),
                .description = container::String("Copy directory recursively (default: false)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string src = args.at("src").get<std::string>();
            std::string dst = args.at("dst").get<std::string>();
            bool recursive = args.value("recursive", false);

            std::error_code ec;
            if (std::filesystem::is_directory(src)) {
                if (!recursive) {
                    return container::String(Json{{"success", false}, {"error", "Source is a directory. Set recursive=true."}}.dump().c_str());
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
                return container::String(Json{{"success", false}, {"error", ec.message()}}.dump().c_str());
            }
            log::debug_fmt("copy_file: {} -> {}", src, dst);
            return container::String(Json{{"success", true}, {"src", src}, {"dst", dst}}.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("file_info"),
        container::String("Get file/directory information: existence, type, size, modification time"),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Path to check")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::filesystem::path p(path);

            if (!std::filesystem::exists(p)) {
                return container::String(Json{{"exists", false}, {"path", path}}.dump().c_str());
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

            return container::String(info.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("search_files"),
        container::String("Search for files by name pattern (glob). Returns matching file paths."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Root directory to search from")
            }},
            {container::String("pattern"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Glob pattern to match (e.g. *.cpp, **/*.hpp)")
            }},
            {container::String("recursive"), ToolParameterSchema{
                .type = container::String("boolean"),
                .description = container::String("Search recursively in subdirectories (default: true)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string pattern = args.at("pattern").get<std::string>();
            bool recursive = args.value("recursive", true);

            if (!std::filesystem::exists(path)) {
                return container::String(Json{{"matches", Json::array()}, {"count", 0},
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
            return container::String(Json{{"matches", matches}, {"count", count}, {"truncated", truncated}}.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("grep_content"),
        container::String("Search file contents by regex pattern. Returns matching lines with file paths and line numbers."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Root directory to search in")
            }},
            {container::String("pattern"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Regex pattern to search for")
            }},
            {container::String("file_pattern"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Only search files matching this glob (default: *)")
            }},
            {container::String("max_results"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Maximum number of results (default: 50)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string pattern = args.at("pattern").get<std::string>();
            std::string file_pattern = args.value("file_pattern", "*");
            int max_results = args.value("max_results", 50);

            if (!std::filesystem::exists(path)) {
                return container::String(Json{{"results", Json::array()}, {"error", "Path does not exist: " + path}}.dump().c_str());
            }

            std::regex re;
            try {
                re = std::regex(pattern);
            } catch (const std::regex_error& e) {
                return container::String(Json{{"results", Json::array()}, {"error", "Invalid regex: " + std::string(e.what())}}.dump().c_str());
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
            return container::String(Json{{"results", results}, {"count", total}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_replace_tools
// ════════════════════════════════════════════════════════════════════

void register_replace_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("replace_in_file"),
        container::String("Replace exact text in a file. First match of old is replaced with new. "
            "Include 2-3 lines of surrounding context in old for uniqueness. "
            "If exact match fails, falls back to whitespace-normalized matching."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("File path to edit")
            }},
            {container::String("old"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Exact text to replace (must be unique in file)")
            }},
            {container::String("new"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Replacement text")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string old_str = args.at("old").get<std::string>();
            std::string new_str = args.at("new").get<std::string>();

            std::error_code ec;
            auto fsize = std::filesystem::file_size(path, ec);
            if (ec) {
                return container::String(Json{{"success", false}, {"error", "Cannot read: " + path}}.dump().c_str());
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
                    return container::String(Json{{"success", false},
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
                    return container::String(Json{{"success", false},
                        {"error", "old_string not found in file"}}.dump().c_str());
                }
            }
            if (content.find(old_str, pos + old_str.size()) != std::string::npos) {
                return container::String(Json{{"success", false},
                    {"error", "old_string matches multiple locations. Add more context to make it unique."}}.dump().c_str());
            }

            content.replace(pos, old_str.size(), new_str);

            std::filesystem::copy_file(path, path + ".bak",
                std::filesystem::copy_options::overwrite_existing, ec);

            std::ofstream out_file(path, std::ios::binary | std::ios::trunc);
            if (!out_file) {
                return container::String(Json{{"success", false},
                    {"error", "Cannot write: " + path}}.dump().c_str());
            }
            out_file.write(content.data(), static_cast<std::streamsize>(content.size()));

            int old_lines = static_cast<int>(std::count(old_str.begin(), old_str.end(), '\n')) + 1;
            int new_lines = static_cast<int>(std::count(new_str.begin(), new_str.end(), '\n')) + 1;
            auto summary = std::string(used_fuzzy ? "(fuzzy match) " : "")
                         + "Replaced " + std::to_string(old_lines) + " line(s) with "
                         + std::to_string(new_lines);
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_search_content_tools
// ════════════════════════════════════════════════════════════════════

void register_search_content_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("search_content"),
        container::String("Search files for a literal string (not regex). Returns matching lines with file, line, and column."),
        {
            {container::String("path"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Directory to search in")
            }},
            {container::String("query"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Literal text to search (case-sensitive)")
            }},
            {container::String("file_pattern"), ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Glob pattern for file filtering (default: *)")
            }},
            {container::String("max_results"), ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Max results (default: 50)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string query = args.at("query").get<std::string>();
            std::string file_pattern = args.value("file_pattern", "*");
            int max_results = args.value("max_results", 50);
            if (query.empty()) {
                return container::String(Json{{"results", Json::array()}, {"error", "query is empty"}}.dump().c_str());
            }
            if (!std::filesystem::exists(path)) {
                return container::String(Json{{"results", Json::array()}, {"error", "Path not found: " + path}}.dump().c_str());
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
            return container::String(Json{{"results", results}, {"count", total}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_env_tools
// ════════════════════════════════════════════════════════════════════

void register_env_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("env_get"),
        container::String("Get an environment variable. Returns null if not set."),
        {{container::String("name"), {container::String("string"), container::String("Variable name")}}},
        [](const Json& args) -> container::String {
            std::string name = args.at("name").get<std::string>();
            const char* val = std::getenv(name.c_str());
            return container::String(Json{{"name", name}, {"exists", val != nullptr},
                {"value", val ? val : ""}}.dump().c_str());
        }
    );

    registry.register_tool(
        container::String("env_set"),
        container::String("Set an environment variable for this session (not persistent)."),
        {
            {container::String("name"), {container::String("string"), container::String("Variable name")}},
            {container::String("value"), {container::String("string"), container::String("Variable value")}}
        },
        [](const Json& args) -> container::String {
            std::string name = args.at("name").get<std::string>();
            std::string value = args.at("value").get<std::string>();
            base::platform::compat::setenv_c(name.c_str(), value.c_str(), 1);
            return container::String(Json{{"success", true}, {"name", name}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  mark_read_only_tools
// ════════════════════════════════════════════════════════════════════

void mark_read_only_tools(ToolRegistry& registry) {
    static const char* read_only[] = {
        "read_file", "list_directory", "file_info",
        "search_files", "grep_content", "search_content",
        "read_image",
        "http_get",
        "env_get",
        "memory_search", "memory_read",
        "get_skill", "list_skills",
        "get_workflow_status", "list_workflow_templates",
        "load_workflow_template", "get_workflow_metrics",
        "list_pending_approvals", "export_workflow", "visualize_workflow",
    };
    for (auto name : read_only) {
        registry.set_read_only(name, true);
    }
}

// ════════════════════════════════════════════════════════════════════
//  read_image
// ════════════════════════════════════════════════════════════════════

void register_image_tools(ToolRegistry& registry) {
    registry.register_tool(
        container::String("read_image"),
        container::String("Read an image file and return base64-encoded content with metadata. "
            "Supports PNG, JPEG, GIF, WebP, BMP formats."),
        {{container::String("path"), {container::String("string"), container::String("Image file path")}}},
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                return container::String(Json{{"success", false}, {"error", "Cannot open: " + path}}.dump().c_str());
            }
            auto size = static_cast<size_t>(file.tellg());
            file.seekg(0);
            std::string data(size, '\0');
            file.read(data.data(), static_cast<std::streamsize>(size));

            auto ext = std::filesystem::path(path).extension().string();
            std::string mime = "image/png";
            if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";
            else if (ext == ".gif") mime = "image/gif";
            else if (ext == ".webp") mime = "image/webp";
            else if (ext == ".bmp") mime = "image/bmp";

            static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string b64;
            b64.reserve(((size + 2) / 3) * 4);
            for (size_t i = 0; i < size; i += 3) {
                uint32_t n = static_cast<uint8_t>(data[i]) << 16;
                if (i + 1 < size) n |= static_cast<uint8_t>(data[i + 1]) << 8;
                if (i + 2 < size) n |= static_cast<uint8_t>(data[i + 2]);
                b64 += chars[(n >> 18) & 63];
                b64 += chars[(n >> 12) & 63];
                b64 += (i + 1 < size) ? chars[(n >> 6) & 63] : '=';
                b64 += (i + 2 < size) ? chars[n & 63] : '=';
            }

            log::debug_fmt("read_image: {} ({} bytes, {})", path, size, mime);
            return container::String(Json{{"success", true}, {"path", path},
                {"size", static_cast<int64_t>(size)}, {"mime_type", mime},
                {"data", "data:" + mime + ";base64," + b64}}.dump().c_str());
        }
    );
}

// ════════════════════════════════════════════════════════════════════
//  register_builtin_tools
// ════════════════════════════════════════════════════════════════════

void register_builtin_tools(ToolRegistry& registry, int command_timeout) {
    register_file_tools(registry);
    register_replace_tools(registry);
    register_search_content_tools(registry);
    register_shell_tools(registry, command_timeout);
    register_extended_tools(registry);
    register_env_tools(registry);
    register_image_tools(registry);
    mark_read_only_tools(registry);
}

}  // namespace ben_gear::tools
