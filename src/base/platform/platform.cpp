#include "ben_gear/base/platform/os.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if BEN_GEAR_PLATFORM_MACOS
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#elif BEN_GEAR_PLATFORM_LINUX
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

#if BEN_GEAR_PLATFORM_POSIX
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ben_gear::base::platform {

// ==================== os ====================

std::string os::getenv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value ? value : "";
}

void os::setenv(const std::string& name, const std::string& value) {
    compat::setenv_c(name.c_str(), value.c_str(), 1);
}

std::optional<std::string> os::getenv_optional(const std::string& name) {
#if BEN_GEAR_PLATFORM_WINDOWS
    char* value = nullptr;
    std::size_t size = 0;
    const auto result = _dupenv_s(&value, &size, name.c_str());
    if (result != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string output(value, size > 0 ? size - 1 : 0);
    std::free(value);
    return output;
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::string os::home_directory() {
#if BEN_GEAR_PLATFORM_WINDOWS
    if (auto profile = getenv_optional("USERPROFILE")) {
        return *profile;
    }
    auto drive = getenv_optional("HOMEDRIVE").value_or("");
    auto path = getenv_optional("HOMEPATH").value_or("");
    if (!drive.empty() || !path.empty()) {
        return drive + path;
    }
#else
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }
#endif
    return "/";
}

std::string os::config_directory() {
#if BEN_GEAR_PLATFORM_WINDOWS
    const char* appdata = std::getenv("APPDATA");
    if (appdata && appdata[0] != '\0') {
        return appdata;
    }
    return home_directory() + "\\AppData\\Roaming";
#elif BEN_GEAR_PLATFORM_MACOS
    return home_directory() + "/Library/Application Support";
#else
    // XDG Base Directory Specification
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        return xdg_config;
    }
    return home_directory() + "/.config";
#endif
}

std::string os::temp_directory() {
#if BEN_GEAR_PLATFORM_WINDOWS
    const char* temp = std::getenv("TEMP");
    if (temp && temp[0] != '\0') {
        return temp;
    }
    const char* tmp = std::getenv("TMP");
    if (tmp && tmp[0] != '\0') {
        return tmp;
    }
    return "C:\\Windows\\Temp";
#else
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir && tmpdir[0] != '\0') {
        return tmpdir;
    }
    return "/tmp";
#endif
}

std::string os::current_directory() {
    char buffer[4096];
    if (compat::getcwd(buffer, sizeof(buffer))) {
        return buffer;
    }
    return ".";
}

bool os::create_directory(const std::string& path) {
    return compat::mkdir_no_mode(path.c_str()) == 0 || errno == EEXIST;
}

bool os::remove_directory(const std::string& path) {
    return compat::rmdir(path.c_str()) == 0;
}

std::string os::hostname() {
    char buffer[256];
#if BEN_GEAR_PLATFORM_WINDOWS
    // Windows: GetComputerNameA 不需要 Winsock
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return buffer;
    }
#else
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return buffer;
    }
#endif
    return "localhost";
}

std::string os::username() {
    const char* user = std::getenv("USER");
    if (user && user[0] != '\0') {
        return user;
    }
#if BEN_GEAR_PLATFORM_WINDOWS
    user = std::getenv("USERNAME");
    if (user && user[0] != '\0') {
        return user;
    }
#endif
    return "unknown";
}

// ==================== cpu ====================

size_t cpu::frequency_mhz() {
#if BEN_GEAR_PLATFORM_MACOS
    char buffer[256];
    size_t size = sizeof(buffer);
    if (sysctlbyname("hw.cpufrequency", buffer, &size, nullptr, 0) == 0) {
        uint64_t freq = 0;
        std::memcpy(&freq, buffer, sizeof(freq));
        return static_cast<size_t>(freq / 1000000);
    }
#elif BEN_GEAR_PLATFORM_LINUX
    FILE* file = fopen("/proc/cpuinfo", "r");
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "cpu MHz", 7) == 0) {
                fclose(file);
                const char* colon = strchr(line, ':');
                if (colon) {
                    return static_cast<size_t>(std::atof(colon + 1));
                }
            }
        }
        fclose(file);
    }
#elif BEN_GEAR_PLATFORM_WINDOWS
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        if (RegQueryValueExA(key, "~MHz", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&mhz), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return static_cast<size_t>(mhz);
        }
        RegCloseKey(key);
    }
#endif
    return 2400;
}

// ==================== thread ====================

void thread::set_name(const std::string& name) {
#if BEN_GEAR_PLATFORM_MACOS
    pthread_setname_np(name.c_str());
#elif BEN_GEAR_PLATFORM_LINUX
    pthread_setname_np(pthread_self(), name.c_str());
#elif BEN_GEAR_PLATFORM_WINDOWS
    // Windows 10+: SetThreadDescription
    // 需要动态加载，因为旧版 Windows 没有
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32) {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        auto fn = reinterpret_cast<SetThreadDescriptionFn>(
            GetProcAddress(kernel32, "SetThreadDescription"));
        if (fn) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                auto wname = std::make_unique<wchar_t[]>(wlen);
                MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname.get(), wlen);
                fn(GetCurrentThread(), wname.get());
            }
        }
    }
#endif
}

void thread::set_affinity(int core) {
#if BEN_GEAR_PLATFORM_LINUX
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif BEN_GEAR_PLATFORM_MACOS
    // macOS 不支持线程亲和性
    (void)core;
#elif BEN_GEAR_PLATFORM_WINDOWS
    SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << core);
#endif
}

size_t thread::current_id() {
    static std::atomic<size_t> next_id{1};
    thread_local size_t id = next_id.fetch_add(1);
    return id;
}

// ==================== process ====================

size_t process::current_pid() {
    return compat::get_pid();
}

size_t process::parent_pid() {
    return compat::get_parent_pid();
}

process::ExecuteResult process::execute(const std::string& command) {
    ExecuteResult result;

    FILE* pipe = compat::popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result.stdout_output += buffer.data();
    }

    result.exit_code = compat::pclose(pipe);
    return result;
}

void process::exit(int code) {
    std::exit(code);
}

// ==================== subprocess ====================

#if BEN_GEAR_PLATFORM_WINDOWS

subprocess::Process subprocess::spawn(const std::string& program,
                                       const std::vector<std::string>& argv,
                                       const std::vector<std::string>& env) {
    Process proc;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdin_write = nullptr;
    HANDLE child_stdout_read = nullptr;
    HANDLE child_stdout_write = nullptr;

    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) return proc;
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return proc;
    }

    // 父进程端不可继承
    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    // 构建命令行
    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) cmdline += ' ';
        cmdline += '"';
        for (char c : argv[i]) {
            if (c == '"') cmdline += '\\';
            cmdline += c;
        }
        cmdline += '"';
    }

    // 构建环境块
    std::string env_block;
    if (!env.empty()) {
        // 先复制当前环境
        auto* env_strings = GetEnvironmentStrings();
        if (env_strings) {
            const char* p = env_strings;
            while (*p) {
                auto len = strlen(p);
                env_block.append(p, len);
                env_block.push_back('\0');
                p += len + 1;
            }
            FreeEnvironmentStrings(env_strings);
        }
        // 追加额外环境变量（覆盖同名的）
        for (const auto& e : env) {
            env_block += e;
            env_block.push_back('\0');
        }
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stdout_write;
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    const char* env_ptr = env_block.empty() ? nullptr : env_block.c_str();

    if (!CreateProcessA(
            program.c_str(),
            cmdline.data(),
            nullptr, nullptr, TRUE,
            0, env_ptr, nullptr, &si, &pi)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return proc;
    }

    // 关闭子进程端的句柄
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);

    proc.child_stdin_fd = -1;
    proc.child_stdout_fd = -1;
    proc.process_handle = pi.hProcess;
    proc.thread_handle = pi.hThread;

    // 将 stdout 读端包装为 FILE*
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(child_stdout_read), _O_RDONLY | _O_TEXT);
    if (fd < 0) {
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdin_write);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return proc;
    }
    proc.pipe = _fdopen(fd, "r");

    // TODO: stdin 写端暂存 child_stdin_write，MCP 当前用 fprintf(pipe_) 写入
    // 未来需要将 stdin 和 stdout 分离为独立 FILE*
    CloseHandle(child_stdin_write);

    return proc;
}

void subprocess::close(Process& proc) {
    if (proc.pipe) {
        fclose(proc.pipe);
        proc.pipe = nullptr;
    }
    if (proc.process_handle) {
        WaitForSingleObject(proc.process_handle, 5000);
        CloseHandle(proc.process_handle);
        proc.process_handle = nullptr;
    }
    if (proc.thread_handle) {
        CloseHandle(proc.thread_handle);
        proc.thread_handle = nullptr;
    }
}

#else  // POSIX

subprocess::Process subprocess::spawn(const std::string& program,
                                       const std::vector<std::string>& argv,
                                       const std::vector<std::string>& env) {
    Process proc;

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) < 0) return proc;
    if (pipe(stdout_pipe) < 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        return proc;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        return proc;
    }

    if (pid == 0) {
        // 子进程
        ::close(stdin_pipe[1]);    // 关闭写端
        ::close(stdout_pipe[0]);   // 关闭读端

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        ::close(stdin_pipe[0]);
        ::close(stdout_pipe[1]);

        // 设置额外环境变量
        for (const auto& e : env) {
            auto eq = e.find('=');
            if (eq != std::string::npos) {
                ::setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
            }
        }

        // 构建 argv 数组
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (auto& a : const_cast<std::vector<std::string>&>(argv)) {
            c_argv.push_back(a.data());
        }
        c_argv.push_back(nullptr);

        execvp(program.c_str(), c_argv.data());
        _exit(127);
    }

    // 父进程
    ::close(stdin_pipe[0]);   // 关闭读端
    ::close(stdout_pipe[1]);  // 关闭写端

    proc.pid = pid;
    proc.child_stdin_fd = stdin_pipe[1];
    proc.child_stdout_fd = stdout_pipe[0];
    proc.pipe = fdopen(stdout_pipe[0], "r");

    return proc;
}

void subprocess::close(Process& proc) {
    if (proc.pipe) {
        fclose(proc.pipe);
        proc.pipe = nullptr;
    }
    if (proc.child_stdin_fd >= 0) {
        ::close(proc.child_stdin_fd);
        proc.child_stdin_fd = -1;
    }
    if (proc.child_stdout_fd >= 0) {
        // 已被 fclose 关闭，仅清零
        proc.child_stdout_fd = -1;
    }
    if (proc.pid > 0) {
        int status = 0;
        waitpid(proc.pid, &status, 0);
        proc.pid = -1;
    }
}

#endif  // POSIX / Windows

}  // namespace ben_gear::base::platform
