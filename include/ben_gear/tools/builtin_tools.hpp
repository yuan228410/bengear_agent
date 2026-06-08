#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/net/http.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/tools/workflow_tools.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

/// 文件读取工具
inline void register_file_tools(ToolRegistry& registry) {
    // 读取文件
    registry.register_tool(
        base::container::String("read_file"),
        base::container::String("Read file content. Supports text files with UTF-8 encoding."),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("File path to read")
            }},
            {base::container::String("start_line"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("Start line number (1-based, optional)")
            }},
            {base::container::String("end_line"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("End line number (inclusive, optional)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();

            std::ifstream file(path, std::ios::binary);
            if (!file) {
                log::error_fmt("read_file: cannot open: {}", path);
                return container::String(("Error: Cannot open file: " + path).c_str());
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();

            if (args.contains("start_line") || args.contains("end_line")) {
                int start = args.value("start_line", 1);
                int end = args.value("end_line", INT_MAX);

                std::istringstream lines(content);
                std::string line;
                std::string result;
                int line_num = 1;

                while (std::getline(lines, line)) {
                    if (line_num >= start && line_num <= end) {
                        result += std::to_string(line_num) + "|" + line + "\n";
                    }
                    if (line_num > end) break;
                    line_num++;
                }
                return container::String(result.c_str());
            }

            log::debug_fmt("read_file: {} ({} bytes)", path, content.size());
            return container::String(content.c_str());
        }
    );

    // 写入文件
    registry.register_tool(
        base::container::String("write_file"),
        base::container::String("Write content to a file. Supports overwrite, append, and line-range replacement. "
            "Use start_line/end_line to replace specific lines (1-based, inclusive). "
            "Example: start_line=5, end_line=10 replaces lines 5-10 with content."),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("File path to write")
            }},
            {base::container::String("content"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Content to write")
            }},
            {base::container::String("mode"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Write mode: 'overwrite' (default), 'append', or 'replace'")
            }},
            {base::container::String("start_line"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("Start line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }},
            {base::container::String("end_line"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("End line for replace mode (1-based, inclusive). Ignored unless mode='replace'")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string content = args.at("content").get<std::string>();
            std::string mode = args.value("mode", "overwrite");

            // 自动创建父目录
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

            // replace 模式：替换指定行范围
            if (mode == "replace") {
                int start_line = args.value("start_line", 0);
                int end_line = args.value("end_line", 0);

                if (start_line <= 0 || end_line <= 0 || start_line > end_line) {
                    log::error_fmt("write_file replace: invalid line range start={} end={}", start_line, end_line);
                    return container::String("Error: Invalid line range for replace mode");
                }

                // 读取现有文件内容
                std::ifstream in_file(path);
                if (!in_file) {
                    log::error_fmt("write_file replace: cannot open for reading: {}", path);
                    return container::String(("Error: Cannot open file for reading: " + path).c_str());
                }

                std::vector<std::string> lines;
                std::string line;
                while (std::getline(in_file, line)) {
                    lines.push_back(line);
                }
                in_file.close();

                // 将 content 按行分割
                std::vector<std::string> new_lines;
                std::istringstream content_stream(content);
                std::string content_line;
                while (std::getline(content_stream, content_line)) {
                    new_lines.push_back(content_line);
                }

                // 执行行范围替换
                int total_lines = static_cast<int>(lines.size());
                if (start_line > total_lines) {
                    // 起始行超出文件末尾，追加到末尾
                    for (auto& nl : new_lines) {
                        lines.push_back(std::move(nl));
                    }
                } else {
                    int replace_end = std::min(end_line, total_lines);
                    // 删除 [start_line-1, replace_end) 的行，插入新行
                    lines.erase(lines.begin() + start_line - 1, lines.begin() + replace_end);
                    lines.insert(lines.begin() + start_line - 1, 
                                 std::make_move_iterator(new_lines.begin()),
                                 std::make_move_iterator(new_lines.end()));
                }

                // 写回文件
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

            // overwrite / append 模式
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

    // 删除文件
    registry.register_tool(
        base::container::String("delete_file"),
        base::container::String("Delete a file or empty directory"),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("File or directory path to delete")
            }},
            {base::container::String("recursive"), ToolParameterSchema{
                .type = base::container::String("boolean"),
                .description = base::container::String("Recursively delete non-empty directory (default: false)")
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

    // 列出目录
    registry.register_tool(
        base::container::String("list_directory"),
        base::container::String("List contents of a directory"),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Directory path to list")
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
                if (entry.is_directory()) {
                    result += "/";
                }
                result += "\n";
            }

            return result.empty() ? container::String("Empty directory") : container::String(result.c_str());
        }
    );

    // 重命名文件
    registry.register_tool(
        base::container::String("rename_file"),
        base::container::String("Rename or move a file/directory"),
        {
            {base::container::String("src"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Source path")
            }},
            {base::container::String("dst"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Destination path")
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

/// Shell 执行工具（跨平台超时，不依赖 timeout/perl 命令）
inline void register_shell_tools(ToolRegistry& registry, int default_timeout = 30) {
    registry.register_tool(
        base::container::String("execute_command"),
        base::container::String("Execute a shell command and return the output with exit code"),
        {
            {base::container::String("command"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Shell command to execute")
            }},
            {base::container::String("timeout"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("Timeout in seconds (default: 30)")
            }},
            {base::container::String("cwd"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Working directory for the command (optional)")
            }}
        },
        [default_timeout](const Json& args) -> container::String {
            std::string command = args.at("command").get<std::string>();
            int timeout = args.value("timeout", default_timeout);
            std::string cwd = args.value("cwd", "");

            std::string full_cmd;
            if (!cwd.empty()) {
                full_cmd += "cd \"";
                full_cmd += cwd;
                full_cmd += "\" && ";
            }
            full_cmd += command;

            log::info_fmt("execute_command: {} (cwd={} timeout={}s)", command, cwd.empty() ? "." : cwd, timeout);
            auto start = std::chrono::steady_clock::now();
            int exit_code = -1;
            bool timed_out = false;
            std::string result;

#if BEN_GEAR_PLATFORM_WINDOWS
            // Windows：CreateProcess + 管道 + TerminateProcess
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

            std::string cmd_line = full_cmd + " 2>&1";
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

            // 读输出
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
            // 读完剩余
            for (;;) {
                DWORD avail = 0;
                if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
                if (!ReadFile(read_end, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0) break;
                result.append(buffer, bytes_read);
            }
            CloseHandle(read_end);

            // 超时检查
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
            // POSIX：fork + exec + pipe，精确控制子进程生命周期
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
                // 子进程：设为新进程组，超时时杀整个组
                setpgid(0, 0);
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", full_cmd.c_str(), nullptr);
                _exit(127);
            }

            close(pipefd[1]);

            // 读管道 + 超时监控并行
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

            // 等待子进程，带超时轮询
            int status = 0;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
            while (std::chrono::steady_clock::now() < deadline) {
                if (read_done.load(std::memory_order_acquire)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (!read_done.load(std::memory_order_acquire)) {
                // 超时，杀掉整个进程组（sh + 所有子进程）
                kill(-pid, SIGKILL);
                // 关闭读端中断 read()
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

/// HTTP 工具
/// http_get/http_post 使用共享 IoContext 的 EventLoop 发起请求
/// 通过 sync_wait 在 EventLoop 线程上运行协程，避免创建局部 EventLoop
inline void register_http_tools(ToolRegistry& registry, net::IoContext& io_ctx) {

    registry.register_tool(
        base::container::String("http_get"),
        base::container::String("Perform an HTTP GET request and return the response"),
        {
            {base::container::String("url"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("URL to fetch")
            }},
            {base::container::String("headers"), ToolParameterSchema{
                .type = base::container::String("array"),
                .description = base::container::String("Optional HTTP headers (array of 'Key: Value' strings)")
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
            // HTTP 请求重试：TLS 握手失败、连接重置等瞬态错误自动重试
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
                    // 瞬态错误：TLS 握手失败、连接重置、超时 — 可重试
                    bool transient = err.find("TLS handshake") != std::string::npos ||
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
        base::container::String("http_post"),
        base::container::String("Perform an HTTP POST request with JSON body"),
        {
            {base::container::String("url"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("URL to post to")
            }},
            {base::container::String("body"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("JSON request body")
            }},
            {base::container::String("headers"), ToolParameterSchema{
                .type = base::container::String("array"),
                .description = base::container::String("Optional HTTP headers (array of 'Key: Value' strings)")
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
            try {
                // 使用共享 IoContext 的 EventLoop
                net::HttpClient client;
                container::Vector<container::String> c_headers;
                for (const auto& h : headers) {
                    c_headers.push_back(container::String(h.c_str()));
                }
                auto response = net::sync_wait(io_ctx.loop(),
                    client.post_json_async(io_ctx.loop(),
                        container::String(std::move(url)),
                        container::String(std::move(body)),
                        std::move(c_headers)));
                log::debug_fmt("http_post: {} -> status={}", url, response.status);
                if (response.status == 0) {
                    return container::String(Json{{"success", false}, {"status", 0}, {"error", "connection failed: no response received"}}.dump().c_str());
                }
                return container::String(Json{{"success", true}, {"status", response.status}, {"body", response.body}}.dump().c_str());
            } catch (const std::exception& e) {
                log::error_fmt("http_post failed: {} - {}", url, e.what());
                return container::String(Json{{"success", false}, {"error", e.what()}}.dump().c_str());
            }
        }
    );
}

/// 扩展工具（mkdir, copy_file, file_info, search_files, grep_content）
inline void register_extended_tools(ToolRegistry& registry) {
    // mkdir
    registry.register_tool(
        base::container::String("mkdir"),
        base::container::String("Create a directory. Creates parent directories by default."),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Directory path to create")
            }},
            {base::container::String("parents"), ToolParameterSchema{
                .type = base::container::String("boolean"),
                .description = base::container::String("Create parent directories as needed (default: true)")
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

    // copy_file
    registry.register_tool(
        base::container::String("copy_file"),
        base::container::String("Copy a file or directory"),
        {
            {base::container::String("src"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Source path")
            }},
            {base::container::String("dst"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Destination path")
            }},
            {base::container::String("recursive"), ToolParameterSchema{
                .type = base::container::String("boolean"),
                .description = base::container::String("Copy directory recursively (default: false)")
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

    // file_info
    registry.register_tool(
        base::container::String("file_info"),
        base::container::String("Get file/directory information: existence, type, size, modification time"),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Path to check")
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

    // search_files
    registry.register_tool(
        base::container::String("search_files"),
        base::container::String("Search for files by name pattern (glob). Returns matching file paths."),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Root directory to search from")
            }},
            {base::container::String("pattern"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Glob pattern to match (e.g. *.cpp, *.hpp)")
            }}
        },
        [](const Json& args) -> container::String {
            std::string path = args.at("path").get<std::string>();
            std::string pattern = args.at("pattern").get<std::string>();

            if (!std::filesystem::exists(path)) {
                return container::String(Json{{"matches", Json::array()}, {"count", 0},
                            {"error", "Path does not exist: " + path}}.dump().c_str());
            }

            Json matches = Json::array();
            int count = 0;
            const int max_results = 100;
            bool truncated = false;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path,
                    std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (truncated) { count++; continue; }

                auto filename = entry.path().filename().string();

                // 简单 glob 匹配：支持 * 通配符
                bool match = false;
                if (pattern.find('*') != std::string::npos) {
                    auto pos = pattern.find('*');
                    std::string prefix = pattern.substr(0, pos);
                    std::string suffix = pattern.substr(pos + 1);
                    if (filename.size() >= prefix.size() + suffix.size()) {
                        match = (filename.substr(0, prefix.size()) == prefix) &&
                                (filename.substr(filename.size() - suffix.size()) == suffix);
                    }
                } else {
                    match = (filename == pattern);
                }

                if (match) {
                    if (static_cast<int>(matches.size()) < max_results) {
                        matches.push_back(entry.path().string());
                    } else {
                        truncated = true;
                    }
                    count++;
                }
            }

            log::debug_fmt("search_files: {} pattern='{}' found={}", path, pattern, count);
            return container::String(Json{{"matches", matches}, {"count", count}, {"truncated", truncated}}.dump().c_str());
        }
    );

    // grep_content
    registry.register_tool(
        base::container::String("grep_content"),
        base::container::String("Search file contents by regex pattern. Returns matching lines with file paths and line numbers."),
        {
            {base::container::String("path"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Root directory to search in")
            }},
            {base::container::String("pattern"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Regex pattern to search for")
            }},
            {base::container::String("file_pattern"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Only search files matching this glob (default: *)")
            }},
            {base::container::String("max_results"), ToolParameterSchema{
                .type = base::container::String("integer"),
                .description = base::container::String("Maximum number of results (default: 50)")
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

                // 文件名过滤
                if (file_pattern != "*") {
                    auto filename = entry.path().filename().string();
                    auto pos = file_pattern.find('*');
                    if (pos != std::string::npos) {
                        std::string prefix = file_pattern.substr(0, pos);
                        std::string suffix = file_pattern.substr(pos + 1);
                        if (filename.size() < prefix.size() + suffix.size() ||
                            filename.substr(0, prefix.size()) != prefix ||
                            filename.substr(filename.size() - suffix.size()) != suffix) {
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
                    } catch (...) { break; }
                }
            }

            log::debug_fmt("grep_content: {} pattern='{}' found={}", path, pattern, total);
            return container::String(Json{{"results", results}, {"count", total}}.dump().c_str());
        }
    );
}

/// 注册所有内置工具
inline void register_builtin_tools(ToolRegistry& registry, int command_timeout = 30) {
    register_file_tools(registry);
    register_shell_tools(registry, command_timeout);
    // HTTP 工具需要 IoContext，由 SharedResources::post_init() 单独注册
    register_extended_tools(registry);
    // 工作流工具由 SharedResources::post_init() 单独注册
}

}  // namespace ben_gear::tools
