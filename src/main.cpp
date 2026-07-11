#include "cli/app.hpp"
#include "cli/repl/terminal_io.hpp"
#include "base/log/logger.hpp"
#include "base/platform/os.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#ifdef _WIN32
#pragma comment(lib, "dbghelp.lib")
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace {

static void write_stderr(const char* data) {
    ben_gear::base::platform::compat::write_stderr(data, strlen(data));
}

#ifdef _WIN32
static void crash_handler(int sig) {
    ben_gear::cli::restore_terminal_on_crash();

    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    const char* sig_name = sig == SIGSEGV ? "SIGSEGV"
        : sig == SIGABRT ? "SIGABRT"
        : sig == SIGILL ? "SIGILL"
        : "UNKNOWN";
    char buf[512];
    snprintf(buf, sizeof(buf), "\n!!! CRASH: signal=%d (%s) !!!\n", sig, sig_name);
    write_stderr(buf);

    void* frames[64];
    WORD n = CaptureStackBackTrace(0, 64, frames, nullptr);

    for (WORD i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "#%2d 0x%014llx  ??\n",
                 i, reinterpret_cast<unsigned long long>(frames[i]));
        write_stderr(buf);
    }

    _exit(sig);
}
#else
static void crash_handler(int sig) {
    // 恢复终端状态（避免崩溃后终端卡在 raw mode）
    ben_gear::cli::restore_terminal_on_crash();

    // 重置信号处理器，避免递归
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    const char* sig_name = sig == SIGSEGV ? "SIGSEGV"
        : sig == SIGBUS ? "SIGBUS"
        : sig == SIGABRT ? "SIGABRT"
        : sig == SIGILL ? "SIGILL"
        : "UNKNOWN";
    char buf[512];
    snprintf(buf, sizeof(buf), "\n!!! CRASH: signal=%d (%s) !!!\n", sig, sig_name);
    write_stderr(buf);

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

    // 输出每帧的地址和 dladdr 信息
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

        // Linux/ELF PIE 下，addr2line 需要相对主模块基址的偏移。
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

    _exit(sig);
}
#endif

static void install_crash_handler() {
    signal(SIGSEGV, crash_handler);
#ifndef _WIN32
    signal(SIGBUS, crash_handler);
#endif
    signal(SIGABRT, crash_handler);
    signal(SIGILL, crash_handler);
}

}  // namespace

int main(int argc, char** argv) {
    ben_gear::base::platform::compat::init_console_utf8();
    install_crash_handler();
    try {
        return ben_gear::cli::run_cli(argc, argv);
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
