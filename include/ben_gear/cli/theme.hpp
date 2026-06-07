#pragma once

#include <cstdint>

namespace ben_gear::cli {

/// 16色枚举
enum class Color16 : uint8_t {
    black = 30,
    red = 31,
    green = 32,
    yellow = 33,
    blue = 34,
    magenta = 35,
    cyan = 36,
    white = 37,
    bright_black = 90,
    bright_red = 91,
    bright_green = 92,
    bright_yellow = 93,
    bright_blue = 94,
    bright_magenta = 95,
    bright_cyan = 96,
    bright_white = 97,
    none = 0
};

/// RGB 真彩色
struct ColorRGB {
    uint8_t r, g, b;
};

/// 颜色值
struct Color {
    Color16 c16 = Color16::none;
    ColorRGB rgb{};
    bool use_rgb = false;

    static Color from_16(Color16 c) { return {.c16 = c}; }
    static Color from_rgb(uint8_t r, uint8_t g, uint8_t b) {
        return {.c16 = Color16::none, .rgb = {r, g, b}, .use_rgb = true};
    }
    static Color none() { return {}; }
};

/// 文本样式位掩码
enum class StyleFlag : uint8_t {
    none      = 0,
    bold      = 1 << 0,
    dim       = 1 << 1,
    italic    = 1 << 2,
    underline = 1 << 3,
};

inline StyleFlag operator|(StyleFlag a, StyleFlag b) {
    return static_cast<StyleFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool has_flag(StyleFlag flags, StyleFlag flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

/// 主题配置（纯数据，零依赖）
///
/// 暗色终端配色原则：
/// - 正文使用高对比度颜色（bright_white 或近白色 RGB），确保可读性
/// - 次要信息用 dim + 中等亮度色，不抢焦点但依然可辨
/// - 避免 bright_black（深灰底上几乎不可见）
struct Theme {
    // ---- 助手回复 ----
    Color assistant_text = Color::from_16(Color16::bright_white);  // 正文：亮白，高对比度
    StyleFlag assistant_heading_style = StyleFlag::bold;
    Color assistant_heading = Color::from_rgb(0xFF, 0xFF, 0xFF);
    Color assistant_link = Color::from_rgb(0x6B, 0xD6, 0xFD);     // 亮蓝，清晰可辨
    Color assistant_inline_code_bg = Color::from_rgb(0x3C, 0x3C, 0x3C);  // 暗底
    Color assistant_inline_code_text = Color::from_rgb(0xFF, 0xAB, 0x70); // 暖橙，醒目
    Color assistant_code_bg = Color::from_rgb(0x1E, 0x1E, 0x2E);  // 深紫暗底
    Color assistant_code_text = Color::from_16(Color16::bright_white);
    Color assistant_code_lang = Color::from_rgb(0xFF, 0xD8, 0x66); // 亮黄

    // ---- 用户输入 ----
    Color user_prompt = Color::from_16(Color16::bright_green);

    // ---- 工具调用 ----
    Color tool_name = Color::from_rgb(0x6B, 0xD6, 0xFD);          // 亮蓝
    Color tool_args = Color::from_rgb(0x8B, 0x9E, 0xBE);          // 中等亮度灰蓝，可读
    Color tool_success_marker = Color::from_rgb(0x50, 0xFA, 0x7B); // 亮绿
    Color tool_error_marker = Color::from_rgb(0xFF, 0x6E, 0x6E);  // 亮红
    Color tool_error_text = Color::from_rgb(0xFF, 0x6E, 0x6E);

    // ---- Thinking ----
    Color thinking_label = Color::from_rgb(0xFF, 0xD8, 0x66);     // 亮黄，标签醒目
    Color thinking_text = Color::from_rgb(0xBD, 0xBF, 0xD0);      // 中等亮度灰白，可读但不抢焦点

    // ---- 系统/错误 ----
    Color system_info = Color::from_rgb(0x8B, 0x9E, 0xBE);        // 中等灰蓝
    Color error_text = Color::from_rgb(0xFF, 0x6E, 0x6E);         // 亮红

    // ---- 语法高亮（Dracula 风格，暗色终端经典配色）----
    Color hl_keyword = Color::from_rgb(0xFF, 0x79, 0xC6);         // 粉红
    Color hl_string  = Color::from_rgb(0xF1, 0xFA, 0x8C);         // 亮黄绿
    Color hl_comment = Color::from_rgb(0x62, 0x72, 0xA4);         // 灰蓝，可读但不抢眼
    Color hl_number  = Color::from_rgb(0xBD, 0x93, 0xF9);         // 紫色
    Color hl_function = Color::from_rgb(0x50, 0xFA, 0x7B);        // 亮绿
    Color hl_type    = Color::from_rgb(0x8B, 0xE9, 0xFD);         // 亮青

    static Theme default_dark() { return {}; }

    static Theme default_light() {
        Theme t;
        t.assistant_text = Color::from_rgb(0x20, 0x20, 0x20);
        t.assistant_heading = Color::from_rgb(0x1A, 0x1A, 0x2E);
        t.assistant_link = Color::from_rgb(0x03, 0x5B, 0xB5);
        t.assistant_inline_code_bg = Color::from_rgb(0xE8, 0xE8, 0xE8);
        t.assistant_inline_code_text = Color::from_rgb(0xC7, 0x2E, 0x0C);
        t.assistant_code_bg = Color::from_rgb(0xF5, 0xF5, 0xF5);
        t.assistant_code_text = Color::from_rgb(0x20, 0x20, 0x20);
        t.assistant_code_lang = Color::from_rgb(0x98, 0x68, 0x01);
        t.thinking_text = Color::from_rgb(0x65, 0x65, 0x65);
        t.thinking_label = Color::from_rgb(0x98, 0x68, 0x01);
        t.tool_name = Color::from_rgb(0x03, 0x5B, 0xB5);
        t.tool_args = Color::from_rgb(0x55, 0x55, 0x55);
        t.tool_success_marker = Color::from_rgb(0x00, 0x80, 0x00);
        t.tool_error_marker = Color::from_rgb(0xCC, 0x00, 0x00);
        t.tool_error_text = Color::from_rgb(0xCC, 0x00, 0x00);
        t.system_info = Color::from_rgb(0x55, 0x55, 0x55);
        t.error_text = Color::from_rgb(0xCC, 0x00, 0x00);
        t.hl_keyword = Color::from_rgb(0xA6, 0x26, 0xA4);
        t.hl_string  = Color::from_rgb(0x50, 0xA1, 0x4F);
        t.hl_comment = Color::from_rgb(0x8E, 0x8E, 0x8E);
        t.hl_number  = Color::from_rgb(0x98, 0x68, 0x01);
        t.hl_function = Color::from_rgb(0x40, 0x78, 0xD7);
        t.hl_type    = Color::from_rgb(0xC1, 0x84, 0x01);
        return t;
    }
};

}  // namespace ben_gear::cli
