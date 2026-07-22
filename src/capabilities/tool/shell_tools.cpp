#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "platform/os.hpp"
#include "base/utils/json.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#if BEN_GEAR_PLATFORM_POSIX
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

/// 转义双引号和反斜杠，用于 -c "..." 包裹
static std::string escape_for_shell(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r;
}

/// 解析实际使用的 shell：参数 → $SHELL → 平台默认
static std::string resolve_shell(const std::string& shell_arg) {
    if (!shell_arg.empty()) return shell_arg;
    auto env = ben_gear::base::platform::os::getenv_optional("SHELL");
    if (env.has_value() && !env->empty()) return *env;
#if BEN_GEAR_PLATFORM_WINDOWS
    return "cmd.exe";
#elif BEN_GEAR_PLATFORM_MACOS
    // macOS Catalina 起默认 shell 为 zsh
    return "/bin/zsh";
#else
    return "/bin/bash";
#endif
}

/// 判断 shell 类型（仅用于 Windows，POSIX 统一用 -c）
enum class ShellKind { Cmd, PowerShell, Pwsh, Other };

static ShellKind classify_shell(const std::string& shell_path) {
    // 取 basename 做忽略大小写比较
    auto pos = shell_path.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? shell_path : shell_path.substr(pos + 1);
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "cmd" || name == "cmd.exe") return ShellKind::Cmd;
    if (name == "powershell" || name == "powershell.exe") return ShellKind::PowerShell;
    if (name == "pwsh" || name == "pwsh.exe") return ShellKind::Pwsh;
    return ShellKind::Other;
}

void register_shell_tools(ToolRegistry& registry, int default_timeout) {
    registry.register_tool(
        std::string("execute_command"),
        std::string("Execute a shell command and return the output with exit code"),
        {
            {std::string("command"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Shell command to execute")
            }},
            {std::string("timeout"), ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Timeout in seconds (default: 30)")
            }},
            {std::string("cwd"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Working directory for the command (optional)")
            }},
            {std::string("shell"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Shell path (e.g. /bin/bash, pwsh, cmd.exe). "
                    "Defaults to $SHELL env var, then platform default (cmd.exe/pwsh/bash/zsh)")
            }}
        },
        [default_timeout](const Json& args) -> std::string {
            std::string command = args.at("command").get<std::string>();
            int timeout = args.value("timeout", default_timeout);
            std::string cwd = args.value("cwd", "");
            std::string shell = resolve_shell(args.value("shell", ""));

            log::info_fmt("execute_command: {} (cwd={} timeout={}s shell={})",
                command, cwd.empty() ? "." : cwd, timeout, shell);
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
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
            }
            SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.hStdError = write_end;
            si.hStdOutput = write_end;
            si.dwFlags |= STARTF_USESTDHANDLES;
            PROCESS_INFORMATION pi{};

            // 根据 shell 类型构建命令行
            std::string cmd_line;
            switch (classify_shell(shell)) {
            case ShellKind::Cmd:
                // cmd.exe /c 直接拼接，2>&1 是 cmd 语法
                cmd_line = shell + " /c " + command + " 2>&1";
                break;
            case ShellKind::PowerShell:
            case ShellKind::Pwsh:
                // PowerShell: -Command 后用双引号包裹，内部双引号转义
                // 使用 *>&1 捕获所有输出流（不同于 cmd 的 2>&1）
                cmd_line = shell + " -NoProfile -Command \"" + escape_for_shell(command) + " 2>&1 | ForEach-Object { \"$_\" }\"";
                break;
            case ShellKind::Other:
                // 其他 shell（bash、git-bash 等），传给 -c 执行
                cmd_line = shell + " -c \"" + escape_for_shell(command) + " 2>&1\"";
                break;
            }
            if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, TRUE,
                                CREATE_NEW_PROCESS_GROUP, nullptr,
                                cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
                CloseHandle(read_end);
                CloseHandle(write_end);
                log::error_fmt("execute_command: CreateProcess failed");
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
            }
            CloseHandle(write_end);
            CloseHandle(pi.hThread);

            // 读取输出并检测超时
            // 注意：必须把超时检测放在读取循环内，否则 WaitForSingleObject
            // 会在进程跑完后才执行，超时永远不触发
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
            char buffer[4096];
            DWORD bytes_read = 0;

            for (;;) {
                auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (remaining_ms <= 0) {
                    timed_out = true;
                    break;
                }
                DWORD avail = 0;
                if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
                    // 无数据：等待进程退出或超时（最长等 100ms 以保持响应）
                    DWORD wait_ms = static_cast<DWORD>(std::min(remaining_ms, 100LL));
                    DWORD ret = WaitForSingleObject(pi.hProcess, wait_ms);
                    if (ret == WAIT_OBJECT_0) break;
                    if (ret == WAIT_TIMEOUT) continue;
                    break;
                }
                if (!ReadFile(read_end, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0) break;
                result.append(buffer, bytes_read);
            }

            // 排空管道剩余输出
            for (;;) {
                DWORD avail = 0;
                if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
                if (!ReadFile(read_end, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) || bytes_read == 0) break;
                result.append(buffer, bytes_read);
            }
            CloseHandle(read_end);

            if (timed_out) {
                TerminateProcess(pi.hProcess, 1);
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
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
            }

            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                log::error_fmt("execute_command: fork failed");
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
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
                // execl 参数：shell 路径, argv[0], -c, 命令, nullptr
                execl(shell.c_str(), shell.c_str(), "-c", command.c_str(), nullptr);
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

            {
                auto remaining = deadline - std::chrono::steady_clock::now();
                auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
                if (wait_ms <= 0) wait_ms = 50;
                auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
                bool exited = false;
                while (std::chrono::steady_clock::now() < wait_deadline) {
                    pid_t w = waitpid(pid, &status, WNOHANG);
                    if (w == pid) { exited = true; break; }
                    if (w < 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (!exited) {
                    kill(-pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    timed_out = true;
                    exit_code = -1;
                    goto build_result;
                }
            }
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        build_result:
#endif
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }

            if (timed_out) {
                log::error_fmt("execute_command: timed_out after {}ms", elapsed_ms);
                return Json{{"stdout", result}, {"exit_code", -1}, {"success", false}, {"timed_out", true}}.dump();
            }

            bool success = (exit_code == 0);
            if (!success) {
                log::error_fmt("execute_command: exit_code={} elapsed={}ms output_len={}", exit_code, elapsed_ms, result.size());
            } else {
                log::info_fmt("execute_command: exit_code=0 elapsed={}ms output_len={}", elapsed_ms, result.size());
            }

            return Json{{"stdout", result}, {"exit_code", exit_code}, {"success", success}}.dump();
        }
    );
}

} // namespace ben_gear::tools
