#pragma once

#include "ben_gear/base/container/string.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

namespace ben_gear::cli {

/// 按键类型
enum class Key : uint16_t {
    Unknown = 0,
    Char = 1,
    Enter,
    Tab,
    Backspace,
    CtrlC,
    CtrlD,
    CtrlL,
    CtrlU,
    CtrlA,
    CtrlE,
    CtrlK,
    CtrlW,
    CtrlR,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Delete,
    PageUp,
    PageDown,
    ShiftEnter,
};

/// 按键事件
struct KeyEvent {
    Key key = Key::Unknown;
    char ch = '\0';

    bool is_printable() const { return key == Key::Char && ch >= 0x20 && ch < 0x7F; }
    bool is_enter() const { return key == Key::Enter; }
    bool is_interrupt() const { return key == Key::CtrlC; }
    bool is_eof() const { return key == Key::CtrlD; }
};

/// 终端原始 I/O
///
/// 职责单一：raw mode 切换 + 按键读取
/// 延迟进入 raw mode：调用 enable_raw_mode() 时才切换
/// 析构时自动恢复
/// 异步信号安全的终端恢复函数（供 crash_handler 调用）
/// 只做 tcsetattr 系统调用，不涉及 malloc/锁等不安全操作
void restore_terminal_on_crash();

class TerminalIO {
public:
    TerminalIO();
    ~TerminalIO();

    /// 进入 raw mode（首次调用时生效）
    void enable_raw_mode();

    /// 退出 raw mode（恢复原始终端设置）
    void disable_raw_mode();

    /// 是否已在 raw mode
    bool is_raw_mode() const { return raw_mode_; }

    /// 读取一个按键事件（阻塞，需要先 enable_raw_mode）
    KeyEvent read_key();


    static bool is_tty();

    TerminalIO(const TerminalIO&) = delete;
    TerminalIO& operator=(const TerminalIO&) = delete;

    /// 清空输入读取缓冲区
    void clear_read_buffer() {
        read_buf_pos_ = 0;
        read_buf_len_ = 0;
        pushback_count_ = 0;
    }

private:
    bool raw_mode_ = false;
    alignas(void*) unsigned char saved_[256];  // opaque: 足够容纳 termios
    bool saved_valid_ = false;

    int read_byte();
    std::pair<Key, char> parse_escape();
    void pushback(int byte);  // 将字节放回，下次 read_byte() 优先返回

    // 多字节 pushback 缓冲区（环形），支持 parse_escape 回退多个字节
    static constexpr size_t kPushbackSize = 8;
    int pushback_buf_[kPushbackSize] = {-1, -1, -1, -1, -1, -1, -1, -1};
    size_t pushback_count_ = 0;   // 已 pushback 的字节数
    size_t pushback_head_ = 0;    // 下次 read 位置

    // 输入读取缓冲区：批量读取减少 syscall
    static constexpr size_t kReadBufSize = 64;
    unsigned char read_buf_[kReadBufSize] = {};
    size_t read_buf_pos_ = 0;
    size_t read_buf_len_ = 0;
};

}  // namespace ben_gear::cli
