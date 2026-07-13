#include "base/platform/crash_handler.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>

namespace ben_gear::base::platform {

namespace {

/// 全局 crash 回调
CrashCallback g_crash_callback;
/// 防止递归
std::atomic<bool> g_in_handler{false};

void write_stderr(const char* data) {
    compat::write_stderr(data, strlen(data));
}

#ifdef _WIN32

void print_stack_trace() {
    void* frames[64];
    WORD n = CaptureStackBackTrace(0, 64, frames, nullptr);

    char buf[512];
    for (WORD i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "#%2d 0x%014llX  ??\n",
                 i, reinterpret_cast<unsigned long long>(frames[i]));
        write_stderr(buf);
    }
}

void crash_handler_impl(int sig) {
    if (g_in_handler.exchange(true)) _exit(sig);
    _resetstkoflw();

    // 调用上层回调（如恢复终端）
    if (g_crash_callback) g_crash_callback();

    // 恢复默认处理器，避免递归
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    char buf[512];
    snprintf(buf, sizeof(buf), "\n!!! CRASH: signal=%d (%s) !!!\n", sig, signal_name(sig));
    write_stderr(buf);

    print_stack_trace();

    _exit(sig);
}

#else

void print_stack_trace() {
    void* frames[64];
    int n = backtrace(frames, 64);

    // 获取主模块加载基址
    Dl_info main_info{};
    void* base_addr = nullptr;
    const char* exe_path = nullptr;
    if (n > 0 && dladdr(frames[0], &main_info)) {
        base_addr = main_info.dli_fbase;
        exe_path = main_info.dli_fname;
    }

    char buf[512];
    for (int i = 0; i < n; ++i) {
        Dl_info info{};
        if (dladdr(frames[i], &info) && info.dli_sname) {
            ptrdiff_t offset = static_cast<char*>(frames[i]) - static_cast<char*>(info.dli_saddr);
            snprintf(buf, sizeof(buf), "#%2d 0x%014lx  %s+%td  (%s)\n",
                     i, reinterpret_cast<uintptr_t>(frames[i]), info.dli_sname, offset,
                     info.dli_fname ? info.dli_fname : "?");
        } else {
            snprintf(buf, sizeof(buf), "#%2d 0x%014lx  ??\n", i, reinterpret_cast<uintptr_t>(frames[i]));
        }
        write_stderr(buf);
    }

    // 输出符号化命令
    if (exe_path) {
        write_stderr("\n--- To resolve line numbers ---\n");
        // lldb 批量命令
        std::string lldb_cmds;
        for (int i = 0; i < n && i < 30; ++i) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "image lookup -a 0x%014lx\n", reinterpret_cast<uintptr_t>(frames[i]));
            lldb_cmds += cmd;
        }
        lldb_cmds += "quit\n";

        // 写入临时文件，lldb -s 读取
        char tmpfile[64];
        snprintf(tmpfile, sizeof(tmpfile), "/tmp/bengear_crash_%d.cmd", getpid());
        FILE* f = fopen(tmpfile, "w");
        if (f) {
            fwrite(lldb_cmds.c_str(), 1, lldb_cmds.size(), f);
            fclose(f);
            snprintf(buf, sizeof(buf), "lldb -s %s %s\n", tmpfile, exe_path);
            write_stderr(buf);
        }

        // Linux/ELF PIE 下，addr2line 需要相对主模块基址的偏移
        if (base_addr) {
            snprintf(buf, sizeof(buf), "addr2line -Cfipe %s", exe_path);
            write_stderr(buf);
            for (int i = 0; i < n && i < 20; ++i) {
                Dl_info frame_info{};
                if (dladdr(frames[i], &frame_info) && frame_info.dli_fbase == base_addr) {
                    auto rel = static_cast<uintptr_t>(static_cast<char*>(frames[i]) - static_cast<char*>(base_addr));
                    snprintf(buf, sizeof(buf), " 0x%lx", rel);
                    write_stderr(buf);
                }
            }
            write_stderr("\n");
        }
    }
}

void crash_handler_impl(int sig) {
    if (g_in_handler.exchange(true)) _exit(sig);

    // 调用上层回调（如恢复终端）
    if (g_crash_callback) g_crash_callback();

    // 恢复默认处理器，避免递归
    signal(SIGSEGV, SIG_DFL);
#ifdef SIGBUS
    signal(SIGBUS, SIG_DFL);
#endif
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    char buf[512];
    snprintf(buf, sizeof(buf), "\n!!! CRASH: signal=%d (%s) !!!\n", sig, signal_name(sig));
    write_stderr(buf);

    print_stack_trace();

    _exit(sig);
}

#endif

}  // namespace

void register_crash_callback(CrashCallback cb) {
    g_crash_callback = std::move(cb);
}

void install_crash_handler(CrashCallback cb) {
    if (cb) g_crash_callback = std::move(cb);

#ifdef _WIN32
    signal(SIGSEGV, crash_handler_impl);
    signal(SIGABRT, crash_handler_impl);
    signal(SIGILL, crash_handler_impl);
#else
    signal(SIGSEGV, crash_handler_impl);
#ifdef SIGBUS
    signal(SIGBUS, crash_handler_impl);
#endif
    signal(SIGABRT, crash_handler_impl);
    signal(SIGILL, crash_handler_impl);
#endif
}

}  // namespace ben_gear::base::platform
