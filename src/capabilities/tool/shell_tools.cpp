#include "capabilities/tool/builtin_tools.hpp"

#include "base/log/logger.hpp"
#include "base/platform/os.hpp"
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
            }}
        },
        [default_timeout](const Json& args) -> std::string {
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
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
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
                return Json{{"stdout", ""}, {"exit_code", -1}, {"success", false}}.dump();
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
