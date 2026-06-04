#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <thread>

// ── 平台头文件统一收敛 ──────────────────────────────────────
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ben_gear::base::platform {

// ── 平台宏统一收敛 ──────────────────────────────────────────
// 其他文件应使用这些宏，而非直接 _WIN32 / __linux__ / __APPLE__

#define BEN_GEAR_PLATFORM_WINDOWS 0
#define BEN_GEAR_PLATFORM_LINUX   0
#define BEN_GEAR_PLATFORM_MACOS   0
#define BEN_GEAR_PLATFORM_POSIX   0

#ifdef _WIN32
#undef  BEN_GEAR_PLATFORM_WINDOWS
#define BEN_GEAR_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#undef  BEN_GEAR_PLATFORM_MACOS
#define BEN_GEAR_PLATFORM_MACOS 1
#undef  BEN_GEAR_PLATFORM_POSIX
#define BEN_GEAR_PLATFORM_POSIX 1
#elif defined(__linux__)
#undef  BEN_GEAR_PLATFORM_LINUX
#define BEN_GEAR_PLATFORM_LINUX 1
#undef  BEN_GEAR_PLATFORM_POSIX
#define BEN_GEAR_PLATFORM_POSIX 1
#endif

// ── 操作系统接口 ────────────────────────────────────────────
namespace os {

/// 获取环境变量
std::string getenv(const std::string& name);

/// 设置环境变量
void setenv(const std::string& name, const std::string& value);

/// 获取环境变量（optional 版本，用于可能不存在的情况）
std::optional<std::string> getenv_optional(const std::string& name);

/// 获取用户主目录
std::string home_directory();

/// 获取配置目录
std::string config_directory();

/// 获取临时目录
std::string temp_directory();

/// 获取当前工作目录
std::string current_directory();

/// 创建目录
bool create_directory(const std::string& path);

/// 删除目录
bool remove_directory(const std::string& path);

/// 获取主机名
std::string hostname();

/// 获取用户名
std::string username();

}  // namespace os

// ── CPU 相关 ────────────────────────────────────────────────
namespace cpu {

/// 获取 CPU 核心数
inline size_t core_count() {
    return std::thread::hardware_concurrency();
}

/// 获取缓存行大小（字节）
inline size_t cache_line_size() {
    return 64;
}

/// 获取 CPU 频率（MHz，近似值）
size_t frequency_mhz();

}  // namespace cpu

// ── 线程抽象 ────────────────────────────────────────────────
namespace thread {

/// 设置线程名称
void set_name(const std::string& name);

/// 设置线程亲和性（绑定到指定 CPU 核心）
void set_affinity(int core);

/// 获取当前线程 ID
size_t current_id();

/// 让出 CPU
inline void yield() {
    std::this_thread::yield();
}

/// 休眠（毫秒）
inline void sleep_ms(size_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/// 休眠（微秒）
inline void sleep_us(size_t microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

}  // namespace thread

// ── 进程抽象 ────────────────────────────────────────────────
namespace process {

/// 获取进程 ID
size_t current_pid();

/// 获取父进程 ID
size_t parent_pid();

/// 执行命令
struct ExecuteResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

ExecuteResult execute(const std::string& command);

/// 退出进程
[[noreturn]] void exit(int code);

/// 获取环境变量
inline std::string getenv(const std::string& name) {
    return os::getenv(name);
}

}  // namespace process

// ── POSIX 兼容层 ────────────────────────────────────────────
// 将 POSIX API 差异收敛到这里，其他文件不再直接使用平台特定 API

namespace compat {

/// 关闭文件描述符
inline int close_fd(int fd) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_close(fd);
#else
    return ::close(fd);
#endif
}

/// 创建目录（无权限参数版本）
inline int mkdir_no_mode(const char* path) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_mkdir(path);
#else
    return ::mkdir(path, 0755);
#endif
}

/// 删除目录
inline int rmdir(const char* path) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_rmdir(path);
#else
    return ::rmdir(path);
#endif
}

/// 获取当前工作目录
inline char* getcwd(char* buf, size_t size) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_getcwd(buf, static_cast<int>(size));
#else
    return ::getcwd(buf, size);
#endif
}

/// 获取进程 ID
inline size_t get_pid() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return static_cast<size_t>(::GetCurrentProcessId());
#else
    return static_cast<size_t>(::getpid());
#endif
}

/// 获取父进程 ID（Windows 上返回 0，无直接等价物）
inline size_t get_parent_pid() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return 0;  // Windows 无 getppid 等价物
#elif defined(__APPLE__)
    return static_cast<size_t>(::getppid());
#else
    return static_cast<size_t>(::getppid());
#endif
}

/// popen
inline FILE* popen(const char* cmd, const char* mode) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_popen(cmd, mode);
#else
    return ::popen(cmd, mode);
#endif
}

/// pclose
inline int pclose(FILE* stream) {
#if BEN_GEAR_PLATFORM_WINDOWS
    return ::_pclose(stream);
#else
    return ::pclose(stream);
#endif
}

/// 获取环境变量（C API）
inline char* getenv_c(const char* name) {
    return std::getenv(name);
}

/// 设置环境变量（C API）
inline int setenv_c(const char* name, const char* value, int overwrite) {
#if BEN_GEAR_PLATFORM_WINDOWS
    (void)overwrite;
    return ::_putenv((std::string(name) + "=" + value).c_str());
#else
    return ::setenv(name, value, overwrite);
#endif
}

}  // namespace compat

// ── 安全子进程 ──────────────────────────────────────────────
// 不经过 shell，直接 fork+execvp / CreateProcess，避免命令注入

namespace subprocess {

/// 子进程句柄
struct Process {
    FILE* pipe = nullptr;  // 双向管道（读写子进程 stdin/stdout）
#if BEN_GEAR_PLATFORM_WINDOWS
    void* process_handle = nullptr;  // HANDLE
    void* thread_handle = nullptr;   // HANDLE
#else
    pid_t pid = -1;
    int child_stdin_fd = -1;   // 写入子进程 stdin
    int child_stdout_fd = -1;  // 读取子进程 stdout
#endif

    explicit operator bool() const noexcept { return pipe != nullptr; }
};

/// 安全启动子进程
/// @param program 可执行文件路径
/// @param argv    参数列表（含 argv[0]）
/// @param env     额外环境变量（key=value 形式，追加到子进程环境）
/// @return 子进程句柄，失败返回空
Process spawn(const std::string& program,
              const std::vector<std::string>& argv,
              const std::vector<std::string>& env);

/// 关闭子进程
/// @param proc 子进程句柄
void close(Process& proc);

}  // namespace subprocess

}  // namespace ben_gear::base::platform
