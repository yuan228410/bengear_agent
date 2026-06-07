#pragma once

#include "ben_gear/base/container/string.hpp"

#include <cstdint>
#include <string_view>

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

private:
    bool raw_mode_ = false;
    alignas(void*) unsigned char saved_[256];  // opaque: 足够容纳 termios
    bool saved_valid_ = false;

    int read_byte();
    Key parse_escape();
};

}  // namespace ben_gear::cli
