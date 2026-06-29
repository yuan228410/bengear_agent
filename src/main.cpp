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
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#if defined(__linux__)
#include <ucontext.h>
#endif

namespace {

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BENGEAR_ADDRESS_SANITIZER_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define BENGEAR_ADDRESS_SANITIZER_ENABLED 1
#endif

static const char* g_boot_stage = "process_start";
static uintptr_t g_exe_base = 0;
static const char* g_exe_path = "./bengear";
static volatile sig_atomic_t g_handling_crash = 0;

static void write_stderr(const char* data, size_t size) {
    auto written = write(STDERR_FILENO, data, size);
    (void)written;
}

static void write_stderr(const char* data) {
    write_stderr(data, strlen(data));
}

static void write_hex(uintptr_t value) {
    char buf[2 + sizeof(uintptr_t) * 2];
    buf[0] = '0';
    buf[1] = 'x';
    for (size_t i = 0; i < sizeof(uintptr_t) * 2; ++i) {
        unsigned shift = static_cast<unsigned>((sizeof(uintptr_t) * 2 - 1 - i) * 4);
        unsigned digit = static_cast<unsigned>((value >> shift) & 0xF);
        buf[2 + i] = static_cast<char>(digit < 10 ? ('0' + digit) : ('a' + digit - 10));
    }
    write_stderr(buf, sizeof(buf));
}

static void write_uint(int value) {
    char buf[16];
    int pos = 15;
    buf[pos--] = '\0';
    unsigned v = value < 0 ? static_cast<unsigned>(-value) : static_cast<unsigned>(value);
    do {
        buf[pos--] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v != 0 && pos >= 0);
    if (value < 0 && pos >= 0) buf[pos--] = '-';
    write_stderr(&buf[pos + 1], static_cast<size_t>(14 - pos));
}

#if defined(__linux__)
static uintptr_t signal_pc(void* context) {
#if defined(__aarch64__)
    auto* uc = static_cast<ucontext_t*>(context);
    return static_cast<uintptr_t>(uc->uc_mcontext.pc);
#elif defined(__x86_64__)
    auto* uc = static_cast<ucontext_t*>(context);
    return static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#else
    (void)context;
    return 0;
#endif
}
#endif

static const char* signal_name(int sig) {
    switch (sig) {
        case SIGILL: return "SIGILL";
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        default: return "UNKNOWN";
    }
}

static void fatal_signal_handler(int sig, siginfo_t*, void* context) {
    if (g_handling_crash) {
        _exit(128 + sig);
    }
    g_handling_crash = 1;

    signal(SIGILL, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE, SIG_DFL);

    write_stderr("\n!!! CRASH: signal=");
    write_uint(sig);
    write_stderr(" (");
    write_stderr(signal_name(sig));
    write_stderr(") !!!\n");
    write_stderr("stage=");
    write_stderr(g_boot_stage ? g_boot_stage : "?");
    write_stderr("\n");
#if defined(__linux__)
    uintptr_t pc = signal_pc(context);
    write_stderr("pc=");
    write_hex(pc);
    write_stderr("\n");
    if (g_exe_base != 0 && pc >= g_exe_base) {
        uintptr_t rel = pc - g_exe_base;
        write_stderr("relative_pc=");
        write_hex(rel);
        write_stderr("\naddr2line -Cfipe ");
        write_stderr(g_exe_path ? g_exe_path : "./bengear");
        write_stderr(" ");
        write_hex(rel);
        write_stderr("\n");
    }
#else
    (void)context;
#endif
    _exit(128 + sig);
}

static bool address_sanitizer_runtime_present() {
#if defined(BENGEAR_ADDRESS_SANITIZER_ENABLED)
    return true;
#else
    if (std::getenv("ASAN_OPTIONS") != nullptr) return true;
    if (dlsym(RTLD_DEFAULT, "__asan_init") != nullptr) return true;
    if (dlsym(RTLD_DEFAULT, "__asan_report_error") != nullptr) return true;
    return false;
#endif
}

static void install_crash_handler() {
    if (address_sanitizer_runtime_present()) return;

    Dl_info main_info{};
    if (dladdr(reinterpret_cast<void*>(&install_crash_handler), &main_info)) {
        g_exe_base = reinterpret_cast<uintptr_t>(main_info.dli_fbase);
        g_exe_path = main_info.dli_fname ? main_info.dli_fname : "./bengear";
    }

    struct sigaction action{};
    action.sa_sigaction = fatal_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
}

}  // namespace

extern "C" void bengear_diag_stage(const char* stage) noexcept {
    g_boot_stage = stage;
    if (std::getenv("BENGEAR_DIAG_STAGE") != nullptr) {
        write_stderr("[bengear-stage] ");
        write_stderr(stage ? stage : "?");
        write_stderr("\n");
    }
}


int main(int argc, char** argv) {
    g_boot_stage = "install_crash_handler";
    install_crash_handler();
    try {
        g_boot_stage = "run_cli";
        return ben_gear::cli::run_cli(argc, argv);
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
