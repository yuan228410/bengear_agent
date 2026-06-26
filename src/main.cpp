#include "ben_gear/cli/app.hpp"
#include "ben_gear/cli/repl/terminal_io.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <exception>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

static void write_stderr(const char* data, size_t size) {
    auto written = write(STDERR_FILENO, data, size);
    (void)written;
}

static void write_stderr(const char* data) {
    write_stderr(data, strlen(data));
}

static void crash_handler(int sig) {
    // 恢复终端状态（避免崩溃后终端卡在 raw mode）
    ben_gear::cli::restore_terminal_on_crash();

    // 重置信号处理器，避免递归
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);

    const char* sig_name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGABRT ? "SIGABRT" : "UNKNOWN";
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

    // 输出 lldb 符号化命令
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

        // 也输出 atos 命令（某些环境 atos 更方便）
        snprintf(buf, sizeof(buf), "atos -arch arm64 -o %s", exe_path);
        write_stderr(buf);
        if (base_addr) {
            snprintf(buf, sizeof(buf), " -l 0x%014lx", reinterpret_cast<uintptr_t>(base_addr));
            write_stderr(buf);
        }
        for (int i = 0; i < n && i < 20; ++i) {
            snprintf(buf, sizeof(buf), " 0x%014lx", reinterpret_cast<uintptr_t>(frames[i]));
            write_stderr(buf);
        }
        write_stderr("\n");
    }

    _exit(sig);
}

static void install_crash_handler() {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGABRT, crash_handler);
}

}  // namespace

int main(int argc, char** argv) {
    install_crash_handler();
    try {
        return ben_gear::cli::run_cli(argc, argv);
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
