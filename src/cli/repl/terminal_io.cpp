#include "ben_gear/cli/repl/terminal_io.hpp"
#include "ben_gear/base/log/logger.hpp"

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
#include <poll.h>
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

/// 异步信号安全的终端恢复函数（供 crash_handler 调用）
/// 在 ben_gear::cli 命名空间内，可直接访问匿名命名空间中的静态变量
void restore_terminal_on_crash() {
    restore_terminal_global();
}

static void atexit_handler() {
    restore_terminal_global();
}

/// 保存之前的信号处理器，形成链式调用
struct SavedHandler {
    void (*handler)(int) = SIG_DFL;
};
static SavedHandler saved_handlers[32];  // 按信号编号索引

static void signal_handler(int sig) {
    restore_terminal_global();
    // 调用之前的处理器（如 crash_handler），没有则恢复默认处理
    auto prev = saved_handlers[sig].handler;
    if (prev && prev != SIG_DFL && prev != SIG_IGN) {
        prev(sig);  // 链式调用：先恢复终端，再由 crash_handler 打印堆栈
    } else {
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }
}

/// 注册一次性恢复机制（atexit + 信号处理器）
static void ensure_restore_registered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    std::atexit(atexit_handler);

    // 注册常见致命信号
    for (int sig : {SIGINT, SIGTERM, SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGPIPE}) {
        // 保存当前处理器（可能是 crash_handler），然后替换为 signal_handler
        auto prev = ::signal(sig, signal_handler);
        if (prev == SIG_IGN) {
            ::signal(sig, SIG_IGN);
        } else if (prev) {
            saved_handlers[sig].handler = prev;
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
    clear_read_buffer();
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

void TerminalIO::pushback(int byte) {
    if (pushback_count_ >= kPushbackSize) return;  // 缓冲区满，丢弃
    size_t pos = (pushback_head_ + pushback_count_) % kPushbackSize;
    pushback_buf_[pos] = byte;
    ++pushback_count_;
}

int TerminalIO::read_byte() {
    // 优先返回放回的字节（FIFO 顺序）
    if (pushback_count_ > 0) {
        int b = pushback_buf_[pushback_head_];
        pushback_head_ = (pushback_head_ + 1) % kPushbackSize;
        --pushback_count_;
        return b;
    }
    // 从缓冲区消费
    if (read_buf_pos_ < read_buf_len_) {
        return static_cast<int>(read_buf_[read_buf_pos_++]);
    }
    // 缓冲区空，批量读取
    auto n = ::read(STDIN_FILENO, read_buf_, kReadBufSize);
    if (n <= 0) return -1;
    read_buf_len_ = static_cast<size_t>(n);
    read_buf_pos_ = 0;
    return static_cast<int>(read_buf_[read_buf_pos_++]);
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
    // 优先返回放回的字节（FIFO 顺序）
    if (pushback_count_ > 0) {
        int b = pushback_buf_[pushback_head_];
        pushback_head_ = (pushback_head_ + 1) % kPushbackSize;
        --pushback_count_;
        return b;
    }
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

    // 仅记录真正异常的字节：非法控制字符（非 CR/LF/TAB/ESC）
    // 正常 UTF-8 多字节字符的首字节和续字节不打日志，避免中文输入刷屏
    if (c < 0x20 && c != 0x0D && c != 0x0A && c != 0x09 && c != 0x1B && c != 0x03 && c != 0x04) {
        log::warn_fmt("repl: unexpected control byte {} in read_key", c);
    }

    // ESC 序列（0x1B = 27 < 0x20 = 32，必须先于控制键检查）
    if (c == 0x1B) {
#ifndef _WIN32
        auto [key, ch] = parse_escape();
        return {key, ch};
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
            case 'H': return {Key::Up, '\0'};
            case 'P': return {Key::Down, '\0'};
            case 'K': return {Key::Left, '\0'};
            case 'M': return {Key::Right, '\0'};
            case 'G': return {Key::Home, '\0'};
            case 'O': return {Key::End, '\0'};
            case 'S': return {Key::Delete, '\0'};
            case 'I': return {Key::PageUp, '\0'};
            case 'Q': return {Key::PageDown, '\0'};
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
std::pair<Key, char> TerminalIO::parse_escape() {
    // ESC 后短暂等待：有后续字节则为转义序列，否则为独立 ESC
    // 先检查读取缓冲区是否还有数据
    bool buf_has_data = (read_buf_pos_ < read_buf_len_) || (pushback_count_ > 0);
    if (!buf_has_data) {
#ifndef _WIN32
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        // 50ms 超时：足以等待转义序列，又不会让独立 ESC 卡住
        if (::poll(&pfd, 1, 50) <= 0) {
            return {Key::Unknown, '\0'};  // 超时，视为独立 ESC
        }
#endif
    }

    int c = read_byte();
    if (c < 0) return {Key::Unknown, '\0'};
    if (c == 0x1B) return {Key::Unknown, '\0'};

    // 修复：如果 ESC 后跟的字节是 UTF-8 多字节首字节(>=0xC0)或续字节(0x80-0xBF)，
    // 说明这不是方向键序列，而是 IME 发送的 ESC + 中文字符
    // 直接返回 Key::Char + 该字节，让 read_utf8_char() 正确组装完整字符
    if (c >= 0x80) {
        return {Key::Char, static_cast<char>(c)};
    }

    if (c == '[') {
        c = read_byte();
        if (c < 0) return {Key::Unknown, '\0'};

        if (c == '1') {
            int semi = read_byte();
            if (semi == ';') {
                int mod = read_byte();
                int code = read_byte();
                if (mod == '2' && code == 'A') return {Key::ShiftEnter, '\0'};
                // 未识别的修饰序列，pushback code 让后续处理
                if (code >= 0) pushback(code);
                return {Key::Unknown, '\0'};
            }
            if (semi == '~') return {Key::Home, '\0'};
            // '1' 后面不是 ';' 也不是 '~'，pushback semi 和 '1'
            if (semi >= 0) pushback(semi);
            pushback('1');
            return {Key::Unknown, '\0'};
        }

        switch (c) {
            case 'A': return {Key::Up, '\0'};
            case 'B': return {Key::Down, '\0'};
            case 'C': return {Key::Right, '\0'};
            case 'D': return {Key::Left, '\0'};
            case 'H': return {Key::Home, '\0'};
            case 'F': return {Key::End, '\0'};
        }

        if (c == '3') {
            int tilde = read_byte();
            if (tilde == '~') return {Key::Delete, '\0'};
            if (tilde >= 0) pushback(tilde);
            pushback(c);
            return {Key::Unknown, '\0'};
        }
        if (c == '5') {
            int tilde = read_byte();
            if (tilde == '~') return {Key::PageUp, '\0'};
            if (tilde >= 0) pushback(tilde);
            pushback(c);
            return {Key::Unknown, '\0'};
        }
        if (c == '6') {
            int tilde = read_byte();
            if (tilde == '~') return {Key::PageDown, '\0'};
            if (tilde >= 0) pushback(tilde);
            pushback(c);
            return {Key::Unknown, '\0'};
        }

        // 未识别的 ESC [ c 序列，pushback c 让后续处理
        pushback(c);
        pushback(c);
        return {Key::Unknown, '\0'};
    }

    if (c == 'O') {
        c = read_byte();
        switch (c) {
            case 'H': return {Key::Home, '\0'};
            case 'F': return {Key::End, '\0'};
            default:
                // 未识别的 ESC O 序列，pushback c
                if (c >= 0) pushback(c);
                return {Key::Unknown, '\0'};
        }
    }

    // ESC 后跟单个 ASCII 字符（非 [ 和 O），无法识别
    // pushback 该字符，让后续 read_key() 处理
    pushback(c);
    pushback(c);
    return {Key::Unknown, '\0'};
}
#endif

}  // namespace ben_gear::cli
