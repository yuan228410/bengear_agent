#include "ben_gear/test_loop/test_loop_service.hpp"

#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/test_loop/diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ben_gear::test_loop {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}};
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool looks_like_failure_line(const std::string& line) {
    auto lower = lower_copy(line);
    const char* patterns[] = {
        " fail", "failed", "failure", "error:", "fatal:", "exception", "assert", "undefined reference",
        "no such file", "not found", "segmentation fault", "traceback", "expected", "actual"
    };
    for (const auto* pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

} // namespace

Json to_json(const TestCommandSuggestion& suggestion) {
    return Json{{"id", suggestion.id},
                {"command", suggestion.command},
                {"cwd", suggestion.cwd},
                {"reason", suggestion.reason},
                {"confidence", suggestion.confidence}};
}

Json to_json(const TestDiagnostic& diagnostic) {
    return Json{{"path", diagnostic.path},
                {"line", diagnostic.line},
                {"column", diagnostic.column},
                {"end_column", diagnostic.end_column},
                {"severity", diagnostic.severity},
                {"source", diagnostic.source},
                {"code", diagnostic.code},
                {"message", diagnostic.message},
                {"raw", diagnostic.raw},
                {"test_name", diagnostic.test_name},
                {"confidence", diagnostic.confidence}};
}

Json to_json(const TestRunResult& result) {
    Json failures = Json::array();
    for (const auto& line : result.failure_summary) failures.push_back(line);
    Json diagnostics = Json::array();
    for (const auto& diagnostic : result.diagnostics) diagnostics.push_back(to_json(diagnostic));
    return Json{{"success", result.success},
                {"timed_out", result.timed_out},
                {"exit_code", result.exit_code},
                {"elapsed_ms", result.elapsed_ms},
                {"command", result.command},
                {"cwd", result.cwd},
                {"output", result.output},
                {"failure_summary", failures},
                {"diagnostics", diagnostics},
                {"diagnostics_truncated", result.diagnostics_truncated}};
}

TestLoopService::TestLoopService(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

std::filesystem::path TestLoopService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

bool TestLoopService::validate_cwd(const std::string& input, std::filesystem::path& resolved, std::string& error) const {
    auto root = std::filesystem::weakly_canonical(project_root());
    std::filesystem::path cwd = input.empty() ? root : std::filesystem::path(input);
    if (cwd.is_relative()) cwd = root / cwd;
    std::error_code ec;
    cwd = std::filesystem::weakly_canonical(cwd, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    auto root_text = root.string();
    auto cwd_text = cwd.string();
    if (cwd_text != root_text && cwd_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) != 0) {
        error = "cwd escapes workspace";
        return false;
    }
    resolved = cwd;
    return true;
}

std::vector<TestCommandSuggestion> TestLoopService::detect_commands() const {
    std::vector<TestCommandSuggestion> suggestions;
    auto root = project_root();
    auto exists = [&](std::string_view rel) {
        std::error_code ec;
        return std::filesystem::exists(root / std::string(rel), ec);
    };

    auto add = [&](std::string id, std::string command, std::string reason, int confidence) {
        suggestions.push_back(TestCommandSuggestion{std::move(id), std::move(command), ".", std::move(reason), confidence});
    };

    if (exists("build/bengear_tests")) add("bengear-tests", "./build/bengear_tests", "existing BenGear C++ test binary", 100);
    if (exists("build") && exists("CMakeLists.txt")) add("cmake-build", "cmake --build build", "CMake build directory detected", 80);
    if (exists("CMakeLists.txt")) add("cmake-test", "ctest --test-dir build --output-on-failure", "CMake project detected", 70);
    if (exists("package.json")) {
        if (exists("pnpm-lock.yaml")) add("pnpm-test", "pnpm test", "pnpm project detected", 85);
        else if (exists("yarn.lock")) add("yarn-test", "yarn test", "yarn project detected", 80);
        else add("npm-test", "npm test", "npm project detected", 75);
    }
    if (exists("Cargo.toml")) add("cargo-test", "cargo test", "Rust Cargo project detected", 85);
    if (exists("go.mod")) add("go-test", "go test ./...", "Go module detected", 85);
    if (exists("pyproject.toml") || exists("pytest.ini") || exists("tests")) add("pytest", "pytest", "Python tests detected", 65);

    std::sort(suggestions.begin(), suggestions.end(), [](const auto& a, const auto& b) {
        return a.confidence > b.confidence;
    });
    return suggestions;
}

std::vector<std::string> TestLoopService::parse_failures(const std::string& output, int max_items) const {
    std::vector<std::string> failures;
    for (auto line : split_lines(output)) {
        line = trim(std::move(line));
        if (line.empty()) continue;
        if (!looks_like_failure_line(line)) continue;
        failures.push_back(line);
        if (static_cast<int>(failures.size()) >= max_items) break;
    }
    return failures;
}

TestLoopService::CommandResult TestLoopService::run_command(const std::string& command,
                                                            const std::filesystem::path& cwd,
                                                            int timeout_seconds,
                                                            int max_output_bytes) const {
    CommandResult result;
    auto start = std::chrono::steady_clock::now();
    if (timeout_seconds <= 0) timeout_seconds = 120;
    if (timeout_seconds > 3600) timeout_seconds = 3600;
    if (max_output_bytes <= 0) max_output_bytes = 60000;

#if BEN_GEAR_PLATFORM_WINDOWS
    std::string full_cmd = "cd /d \"" + cwd.string() + "\" && " + command + " 2>&1";
    FILE* pipe = _popen(full_cmd.c_str(), "r");
    if (!pipe) {
        result.output = "failed to start command";
        return result;
    }
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (static_cast<int>(result.output.size()) < max_output_bytes) result.output += buffer;
    }
    result.exit_code = _pclose(pipe);
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.output = "failed to create pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.output = "failed to fork";
        return result;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (chdir(cwd.string().c_str()) != 0) _exit(127);
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
            if (static_cast<int>(read_result.size()) < max_output_bytes) {
                auto remaining = static_cast<size_t>(max_output_bytes) - read_result.size();
                read_result.append(buffer, std::min(static_cast<size_t>(n), remaining));
            }
        }
        read_done.store(true, std::memory_order_release);
    });

    int status = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (read_done.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!read_done.load(std::memory_order_acquire)) {
        kill(-pid, SIGKILL);
        close(pipefd[0]);
        reader.join();
        waitpid(pid, &status, 0);
        result.timed_out = true;
        result.exit_code = -1;
        result.output = std::move(read_result);
    } else {
        close(pipefd[0]);
        reader.join();
        waitpid(pid, &status, 0);
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        result.output = std::move(read_result);
    }
#endif

    result.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    if (static_cast<int>(result.output.size()) >= max_output_bytes) result.output += "\n[output truncated]";
    return result;
}

Json TestLoopService::inspect() const {
    Json suggestions = Json::array();
    for (const auto& suggestion : detect_commands()) suggestions.push_back(to_json(suggestion));
    return Json{{"success", true}, {"project_root", project_root().string()}, {"suggestions", suggestions}};
}

Json TestLoopService::run(const std::string& command, const std::string& cwd, int timeout_seconds, int max_output_bytes) const {
    auto trimmed = trim(command);
    if (trimmed.empty()) return error_json("invalid_arguments", "command is required");
    std::filesystem::path resolved_cwd;
    std::string cwd_error;
    if (!validate_cwd(cwd, resolved_cwd, cwd_error)) return error_json("path_outside_workspace", cwd_error);

    log::info_fmt("run_tests: {} (cwd={} timeout={}s)", trimmed, resolved_cwd.string(), timeout_seconds);
    auto run = run_command(trimmed, resolved_cwd, timeout_seconds, max_output_bytes);
    TestRunResult result;
    result.success = run.exit_code == 0 && !run.timed_out;
    result.timed_out = run.timed_out;
    result.exit_code = run.exit_code;
    result.elapsed_ms = run.elapsed_ms;
    result.command = trimmed;
    result.cwd = resolved_cwd.string();
    result.output = std::move(run.output);
    result.failure_summary = parse_failures(result.output);
    auto parsed = parse_diagnostics(result.output, DiagnosticParseOptions{project_root(), resolved_cwd, 100});
    result.diagnostics = std::move(parsed.diagnostics);
    result.diagnostics_truncated = parsed.truncated;
    return to_json(result);
}

} // namespace ben_gear::test_loop
