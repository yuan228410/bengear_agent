#include "agent/core/interfaces.hpp"
#include <filesystem>
#include <chrono>
#include "platform/os.hpp"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <stdexcept>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#endif

namespace ben_gear::agent::core {

// ════════════════════════════════════════════════════════════════════
//  DefaultFileService
// ════════════════════════════════════════════════════════════════════

class DefaultFileService : public IFileService {
public:
    bool exists(const std::filesystem::path& path) const override {
        return std::filesystem::exists(path);
    }

    std::string read(const std::filesystem::path& path) const override {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot open: " + path.string());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    bool write(const std::filesystem::path& path, const std::string& content) override {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        return true;
    }

    bool remove(const std::filesystem::path& path) override {
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    bool mkdir(const std::filesystem::path& path) override {
        std::error_code ec;
        return std::filesystem::create_directories(path, ec);
    }

    std::vector<std::string> ls(const std::filesystem::path& path) const override {
        std::vector<std::string> entries;
        for (const auto& entry : std::filesystem::directory_iterator(path))
            entries.push_back(entry.path().filename().string());
        std::sort(entries.begin(), entries.end());
        return entries;
    }

    bool copy(const std::filesystem::path& from, const std::filesystem::path& to) override {
        std::error_code ec;
        std::filesystem::copy(from, to, ec);
        return !ec;
    }

    bool rename(const std::filesystem::path& from, const std::filesystem::path& to) override {
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        return !ec;
    }
};

// ════════════════════════════════════════════════════════════════════
//  DefaultCommandExecutor
// ════════════════════════════════════════════════════════════════════

class DefaultCommandExecutor : public ICommandExecutor {
public:
    CommandResult run(const std::string& cmd,
                      const std::vector<std::string>& args,
                      const std::string& cwd) override {
        CommandResult result;
        auto start = std::chrono::steady_clock::now();

#if BEN_GEAR_PLATFORM_WINDOWS
        std::string full_cmd = cmd;
        for (const auto& a : args) full_cmd += " " + a;
        if (!cwd.empty()) {
            full_cmd = "cd /d " + cwd + " && " + full_cmd;
        }

        FILE* pipe = _popen(full_cmd.c_str(), "r");
        if (!pipe) {
            result.exit_code = -1;
            result.stderr_str = "popen failed";
            return result;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe))
            result.stdout_str += buf;
        result.exit_code = _pclose(pipe);
#else
        std::string full_cmd = cmd;
        for (const auto& a : args) full_cmd += " " + a;
        if (!cwd.empty()) {
            full_cmd = "cd " + cwd + " && " + full_cmd;
        }

        int out_pipe[2], err_pipe[2];
        if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
            result.exit_code = -1;
            result.stderr_str = "pipe failed";
            return result;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(out_pipe[0]);
            dup2(out_pipe[1], STDOUT_FILENO);
            close(out_pipe[1]);
            close(err_pipe[0]);
            dup2(err_pipe[1], STDERR_FILENO);
            close(err_pipe[1]);
            execl("/bin/sh", "sh", "-c", full_cmd.c_str(), nullptr);
            _exit(127);
        }

        close(out_pipe[1]);
        close(err_pipe[1]);

        char buf[4096];
        ssize_t n;
        while ((n = read(out_pipe[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            result.stdout_str += buf;
        }
        while ((n = read(err_pipe[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            result.stderr_str += buf;
        }
        close(out_pipe[0]);
        close(err_pipe[0]);

        int status;
        waitpid(pid, &status, 0);
        result.exit_code = WEXITSTATUS(status);
#endif

        auto end = std::chrono::steady_clock::now();
        result.exec_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// ════════════════════════════════════════════════════════════════════
//  Factory: 创建默认实例
// ════════════════════════════════════════════════════════════════════

std::shared_ptr<IFileService> make_default_file_service() {
    return std::make_shared<DefaultFileService>();
}

std::shared_ptr<ICommandExecutor> make_default_command_executor() {
    return std::make_shared<DefaultCommandExecutor>();
}

} // namespace ben_gear::agent::core
