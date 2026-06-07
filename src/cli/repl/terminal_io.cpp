#include "ben_gear/cli/repl/terminal_io.hpp"

#include <cstdio>
#include <cstring>
#include <csignal>

// ==================== 平台适配 ====================

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#endif

namespace ben_gear::cli {

// ==================== 全局恢复机制 ====================
//
// 异常退出（信号、_exit、未捕获异常）时，TerminalIO 析构函数可能不会执行。
// 使用全局保存的终端属性 + atexit/信号处理，确保任何情况下都能恢复。
//
// 设计：
// - saved_global_ 在 enable_raw_mode() 时保存
// - atexit_handler 在进程正常退出时恢复
// - 信号处理器在 SIGINT/SIGTERM/SIGSEGV/SIGABRT 时恢复
// - 多个 TerminalIO 实例安全：只有最后一个 enable_raw_mode 的生效

#ifndef _WIN32
static termios saved_global_;
static bool saved_global_valid_ = false;
#else
static DWORD saved_global_mode_ = 0;
static bool saved_global_valid_ = false;
#endif

static void restore_terminal_global() {
#ifndef _WIN32
    if (saved_global_valid_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_global_);
        saved_global_valid_ = false;
    }
#else
    if (saved_global_valid_) {
        auto* h = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(h, saved_global_mode_);
        saved_global_valid_ = false;
    }
#endif
}

static void atexit_handler() {
    restore_terminal_global();
}

static void signal_handler(int sig) {
    restore_terminal_global();
    // 恢复默认信号处理并重新触发，让系统生成核心转储等
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

/// 注册一次性恢复机制（atexit + 信号处理器）
static void ensure_restore_registered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    std::atexit(atexit_handler);

    // 注册常见致命信号
    for (int sig : {SIGINT, SIGTERM, SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGPIPE}) {
        // 忽略已忽略的信号（如 SIGPIPE）
        auto prev = ::signal(sig, signal_handler);
        if (prev == SIG_IGN) {
            ::signal(sig, SIG_IGN);
        }
    }
}

// ==================== 构造/析构 ====================

TerminalIO::TerminalIO() {
    std::memset(saved_, 0, sizeof(saved_));
}

TerminalIO::~TerminalIO() {
    disable_raw_mode();
}

// ==================== POSIX 实现 ====================

#ifndef _WIN32

void TerminalIO::enable_raw_mode() {
    if (raw_mode_) return;
    if (!is_tty()) return;

    auto* orig = reinterpret_cast<termios*>(saved_);
    if (tcgetattr(STDIN_FILENO, orig) != 0) return;
    saved_valid_ = true;

    auto raw = *orig;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // 保留 OPOST：让终端继续处理输出（\n → \r\n），避免光标位置错乱
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_ = true;

    // 保存到全局，确保异常退出时也能恢复
    saved_global_ = *orig;
    saved_global_valid_ = true;
    ensure_restore_registered();
}

void TerminalIO::disable_raw_mode() {
    if (!raw_mode_) return;
    if (saved_valid_) {
        auto* orig = reinterpret_cast<termios*>(saved_);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
    }
    raw_mode_ = false;
    saved_global_valid_ = false;
}

bool TerminalIO::is_tty() {
    return isatty(STDIN_FILENO) != 0;
}

int TerminalIO::read_byte() {
    unsigned char c = 0;
    auto n = ::read(STDIN_FILENO, &c, 1);
    return (n == 1) ? static_cast<int>(c) : -1;
}

// ==================== Windows 实现 ====================

#else

void TerminalIO::enable_raw_mode() {
    if (raw_mode_) return;
    if (!is_tty()) return;

    auto* h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;

    auto* saved_mode = reinterpret_cast<DWORD*>(saved_);
    *saved_mode = mode;
    saved_valid_ = true;

    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
              ENABLE_PROCESSED_INPUT);
    SetConsoleMode(h, mode);
    raw_mode_ = true;

    saved_global_mode_ = mode;
    saved_global_valid_ = true;
    ensure_restore_registered();
}

void TerminalIO::disable_raw_mode() {
    if (!raw_mode_) return;
    if (saved_valid_) {
        auto* saved_mode = reinterpret_cast<DWORD*>(saved_);
        auto* h = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(h, *saved_mode);
    }
    raw_mode_ = false;
    saved_global_valid_ = false;
}

bool TerminalIO::is_tty() {
    return _isatty(_fileno(stdin)) != 0;
}

int TerminalIO::read_byte() {
    if (_kbhit()) {
        return _getch();
    }
    return _getch();
}

#endif

// ==================== 按键解析（跨平台通用）====================

KeyEvent TerminalIO::read_key() {
    int c = read_byte();
    if (c < 0) return {Key::CtrlD, '\0'};

    // ESC 序列（0x1B = 27 < 0x20 = 32，必须先于控制键检查）
    if (c == 0x1B) {
#ifndef _WIN32
        return {parse_escape(), '\0'};
#else
        return {Key::Unknown, '\0'};
#endif
    }

    if (c < 0x20) {
        switch (c) {
            case 0x0D: case 0x0A: return {Key::Enter, '\0'};
            case 0x09: return {Key::Tab, '\0'};
            case 0x03: return {Key::CtrlC, '\0'};
            case 0x04: return {Key::CtrlD, '\0'};
            case 0x0C: return {Key::CtrlL, '\0'};
            case 0x15: return {Key::CtrlU, '\0'};
            case 0x01: return {Key::CtrlA, '\0'};
            case 0x05: return {Key::CtrlE, '\0'};
            case 0x0B: return {Key::CtrlK, '\0'};
            case 0x17: return {Key::CtrlW, '\0'};
            case 0x12: return {Key::CtrlR, '\0'};
            default:   return {Key::Unknown, '\0'};
        }
    }

#ifdef _WIN32
    if (c == 0xE0 || c == 0x00) {
        int c2 = read_byte();
        if (c2 < 0) return {Key::Unknown, '\0'};
        switch (c2) {
            case 'H': return Key::Up;
            case 'P': return Key::Down;
            case 'K': return Key::Left;
            case 'M': return Key::Right;
            case 'G': return Key::Home;
            case 'O': return Key::End;
            case 'S': return Key::Delete;
            case 'I': return Key::PageUp;
            case 'Q': return Key::PageDown;
            default:  return {Key::Unknown, '\0'};
        }
    }
#endif

    if (c == 0x7F) return {Key::Backspace, '\0'};

#ifdef _WIN32
    if (c == 0x08) return {Key::Backspace, '\0'};
#endif

    return {Key::Char, static_cast<char>(c)};
}

// ==================== POSIX ESC 序列解析 ====================

#ifndef _WIN32
Key TerminalIO::parse_escape() {
    int c = read_byte();
    if (c < 0) return Key::Unknown;
    if (c == 0x1B) return Key::Unknown;

    if (c == '[') {
        c = read_byte();
        if (c < 0) return Key::Unknown;

        if (c == '1') {
            int semi = read_byte();
            if (semi == ';') {
                int mod = read_byte();
                int code = read_byte();
                if (mod == '2' && code == 'A') return Key::ShiftEnter;
                return Key::Unknown;
            }
            if (semi == '~') return Key::Home;
            return Key::Unknown;
        }

        switch (c) {
            case 'A': return Key::Up;
            case 'B': return Key::Down;
            case 'C': return Key::Right;
            case 'D': return Key::Left;
            case 'H': return Key::Home;
            case 'F': return Key::End;
        }

        if (c == '3') {
            int tilde = read_byte();
            if (tilde == '~') return Key::Delete;
            return Key::Unknown;
        }
        if (c == '5') {
            int tilde = read_byte();
            if (tilde == '~') return Key::PageUp;
            return Key::Unknown;
        }
        if (c == '6') {
            int tilde = read_byte();
            if (tilde == '~') return Key::PageDown;
            return Key::Unknown;
        }

        return Key::Unknown;
    }

    if (c == 'O') {
        c = read_byte();
        switch (c) {
            case 'H': return Key::Home;
            case 'F': return Key::End;
            default:  return Key::Unknown;
        }
    }

    return Key::Unknown;
}
#endif

}  // namespace ben_gear::cli
