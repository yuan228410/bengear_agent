#include "ben_gear/cli/render/renderer.hpp"
#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/highlight.hpp"
#include "ben_gear/cli/render/markdown.hpp"
#include "ben_gear/cli/render/spinner.hpp"
#include "ben_gear/cli/render/display_config.hpp"

#include <cstdio>
#include <memory>
#include <ctime>
#include <cstdint>

namespace ben_gear::cli {

// ============================================================
// SilentRenderer
// ============================================================
class SilentRenderer final : public Renderer {
public:
    void on_response_start() override {}
    void on_response_end() override {}
    void on_assistant_text(std::string_view) override {}
    void on_thinking(std::string_view) override {}
    void on_error(std::string_view) override {}
    void on_system(std::string_view) override {}
    void on_tool_call(std::string_view, std::string_view, std::string_view) override {}
    void on_tool_result(std::string_view, std::string_view, bool, std::string_view, size_t) override {}
    void on_mode_changed(PlanManager::Mode) override {}
    void on_tool_blocked(std::string_view, std::string_view) override {}
    void on_usage_stats(int, int, double, double, bool, std::string_view, int64_t) override {}
};

// ============================================================
// TerminalRenderer
//
// 流式输出策略：
// - 助手正文 → stdout，Markdown 流式状态机逐 token 即时输出
// - thinking → stderr，逐 token 即时输出 + 缩进
// - 工具调用 → stderr
// - spinner 只在工具执行期间运行，thinking/正文输出时 spinner 已停止
//
// 时间显示策略：
// - thinking 标签后附时间：💭 thinking · 14:32:05（中点分隔）
// - 工具名称后附时间：⚡ tool_name · 14:32:05（中点分隔）
// - 正文首个 token 前附淡色 ── 短横线（回合视觉锚点，不含时间戳）
// ============================================================
class TerminalRenderer final : public Renderer {
public:
    TerminalRenderer(const Theme& theme, const TerminalCapabilities& cap,
                     const DisplayConfig& config)
        : theme_(theme), cap_(cap), config_(config),
          highlighter_(theme_, cap_),
          md_renderer_(theme_, cap_, highlighter_),
          spinner_(theme_, cap_),
          in_thinking_(false),
          in_text_(false),
          text_time_printed_(false),
          thinking_need_prefix_(true),
          thinking_color_on_(false),
          thinking_at_line_start_(true) {}

    ~TerminalRenderer() override {
        spinner_.stop();
        finish_thinking();
        finish_text();
    }

    void on_response_start() override {
        in_thinking_ = false;
        in_text_ = false;
        text_time_printed_ = false;
        thinking_need_prefix_ = true;
        thinking_color_on_ = false;
        thinking_at_line_start_ = true;
        // 启动等待动画
        if (config_.show_spinner) {
            spinner_.start("waiting for response...");
        }
    }

    void on_response_end() override {
        spinner_.stop();
        finish_thinking();
        finish_text();
    }

    void on_assistant_text(std::string_view token) override {
        if (token.empty()) return;
        spinner_.stop();

        finish_thinking();

        if (!in_text_) {
            in_text_ = true;
            text_time_printed_ = false;
        }

        if (!text_time_printed_) {
            text_time_printed_ = true;
        }

        if (config_.markdown_render) {
            auto output = md_renderer_.feed(token);
            if (!output.empty()) write_out(output.data(), output.size());
        } else {
            auto colored = ansi::colorize(token, theme_.assistant_text, StyleFlag::none, cap_);
            write_out(colored.data(), colored.size());
        }
    }

    void on_thinking(std::string_view token) override {
        if (token.empty()) return;

        if (!in_thinking_) {
            spinner_.stop();
            finish_text();
            in_thinking_ = true;
            thinking_need_prefix_ = true;
            thinking_color_on_ = false;
            thinking_at_line_start_ = true;

            // 💭 thinking · 14:32:05
            // 💭 thinking
            {
                auto dim_code = ansi::dim();
                if (!dim_code.empty()) write_err(dim_code.data(), dim_code.size());
                auto label_color = ansi::fg(theme_.thinking_label, cap_);
                if (!label_color.empty()) write_err(label_color.data(), label_color.size());
                auto bold_code = ansi::bold();
                if (!bold_code.empty()) write_err(bold_code.data(), bold_code.size());
                if (cap_.unicode) {
                    write_err("\xf0\x9f\x92\xad ", 5); // 💭
                }
                write_err("thinking ", 9);
                auto reset = ansi::reset();
                if (!reset.empty()) write_err(reset.data(), reset.size());
            }
            write_err("\n", 1);
        }

        auto dim_code = ansi::dim();
        auto fg_code = ansi::fg(theme_.thinking_text, cap_);

        for (size_t i = 0; i < token.size(); ++i) {
            char c = token[i];
            if (c == '\n') {
                if (thinking_color_on_) {
                    auto reset = ansi::reset();
                    if (!reset.empty()) write_err(reset.data(), reset.size());
                    thinking_color_on_ = false;
                }
                write_err("\n", 1);
                thinking_need_prefix_ = true;
                thinking_at_line_start_ = true;
            } else {
                if (thinking_at_line_start_) {
                    write_err("  ", 2);
                    if (!dim_code.empty()) write_err(dim_code.data(), dim_code.size());
                    if (!fg_code.empty()) write_err(fg_code.data(), fg_code.size());
                    thinking_color_on_ = true;
                    thinking_at_line_start_ = false;
                    thinking_need_prefix_ = false;
                }
                write_err(&c, 1);
            }
        }
        fflush(stderr);
    }

    void on_error(std::string_view message) override {
        spinner_.stop();
        finish_thinking();
        finish_text();
        auto colored = ansi::colorize(message, theme_.error_text, StyleFlag::bold, cap_);
        write_err(colored.data(), colored.size());
        write_err("\n", 1);
    }

    void on_system(std::string_view message) override {
        spinner_.stop();
        finish_thinking();
        finish_text();
        auto colored = ansi::colorize(message, theme_.system_info, StyleFlag::dim, cap_);
        write_err(colored.data(), colored.size());
        write_err("\n", 1);
    }

    void on_tool_call(std::string_view id, std::string_view name, std::string_view args_json) override {
        spinner_.stop();
        finish_thinking();
        finish_text();

        // ┌ ⚡ tool_name · 14:32:05
        if (cap_.unicode) {
            write_err("\xe2\x94\x8c ", 4); // ┌
        } else {
            write_err("[ ", 2);
        }
        if (cap_.unicode) {
            write_err("\xe2\x9a\xa1 ", 4); // ⚡
        } else {
            write_err("> ", 2);
        }
        auto name_colored = ansi::colorize(name, theme_.tool_name, StyleFlag::bold, cap_);
        write_err(name_colored.data(), name_colored.size());

        {
            if (cap_.unicode) {
                write_err(" \xc2\xb7 ", 4); // ·
            } else {
                write_err(" - ", 3);
            }
            auto ts = make_timestamp();
            auto ts_colored = ansi::colorize(ts, theme_.system_info, StyleFlag::dim, cap_);
            write_err(ts_colored.data(), ts_colored.size());
        }

        if (config_.show_tool_id && !id.empty()) {
            write_err(" ", 1);
            auto id_colored = ansi::colorize(id, theme_.tool_args, StyleFlag::dim, cap_);
            write_err(id_colored.data(), id_colored.size());
        }
        write_err("\n", 1);

        if (config_.show_tool_args && !args_json.empty()) {
            auto args_colored = ansi::colorize(args_json, theme_.tool_args, StyleFlag::none, cap_);
            if (cap_.unicode) {
                write_err("\xe2\x94\x82 ", 4); // │
            } else {
                write_err("| ", 2);
            }
            write_err(args_colored.data(), args_colored.size());
            write_err("\n", 1);
        }

        if (config_.show_spinner) {
            spinner_.start(name);
        }
    }

    void on_tool_result(std::string_view id, std::string_view name, bool success,
                        std::string_view output, size_t output_size) override {
        (void)id; (void)name;
        spinner_.stop();

        if (cap_.unicode) {
            write_err("\xe2\x94\x94 ", 4); // └
        } else {
            write_err("\\ ", 2);
        }

        if (success) {
            if (cap_.unicode) {
                write_err("\xe2\x9c\x93 ", 4); // ✓
            } else {
                write_err("OK ", 3);
            }
            auto marker = ansi::colorize("ok", theme_.tool_success_marker, StyleFlag::none, cap_);
            write_err(marker.data(), marker.size());
        } else {
            if (cap_.unicode) {
                write_err("\xe2\x9c\x97 ", 4); // ✗
            } else {
                write_err("ERR ", 4);
            }
            auto marker = ansi::colorize("error", theme_.tool_error_marker, StyleFlag::none, cap_);
            write_err(marker.data(), marker.size());
        }

        {
            base::container::String size_str(" ");
            size_t n = output_size;
            if (n < 1024) {
                char buf[20]; int len = 0;
                if (n == 0) { buf[len++] = '0'; }
                else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
                for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
                size_str.append(buf, static_cast<size_t>(len));
                size_str.append("B", 1);
            } else if (n < 1024 * 1024) {
                size_str.append(base::container::String(std::to_string(n / 1024)));
                size_str.append("KB", 2);
            } else {
                size_str.append(base::container::String(std::to_string(n / 1024 / 1024)));
                size_str.append("MB", 2);
            }
            auto size_colored = ansi::colorize(std::string_view(size_str.data(), size_str.size()),
                                               theme_.tool_args, StyleFlag::dim, cap_);
            write_err(size_colored.data(), size_colored.size());
        }

        if (!success && !output.empty()) {
            write_err(" ", 1);
            auto err_text = ansi::colorize(output, theme_.tool_error_text, StyleFlag::none, cap_);
            write_err(err_text.data(), err_text.size());
        }

        write_err("\n", 1);
    }

    // ---- 模式变更 ----

    void on_mode_changed(PlanManager::Mode mode) override {
        if (mode == PlanManager::Mode::planning) {
            // 🔒 Plan mode — read-only
            if (cap_.unicode) {
                write_err("\xf0\x9f\x94\x92 ", 5); // 🔒
            } else {
                write_err("[plan] ", 7);
            }
            auto msg = ansi::colorize("Plan mode \xe2\x80\x94 read-only", // —
                                       theme_.system_info, StyleFlag::dim, cap_);
            write_err(msg.data(), msg.size());
        } else {
            // 🔓 Full access
            if (cap_.unicode) {
                write_err("\xf0\x9f\x94\x93 ", 5); // 🔓
            } else {
                write_err("[normal] ", 9);
            }
            auto msg = ansi::colorize("Full access", theme_.system_info, StyleFlag::dim, cap_);
            write_err(msg.data(), msg.size());
        }
        write_err("\n", 1);
    }

    // ---- 工具拦截 ----

    void on_tool_blocked(std::string_view tool_name, std::string_view reason) override {
        // └ ✗ tool_name — reason
        if (cap_.unicode) {
            write_err("\xe2\x94\x94 ", 4); // └
            write_err("\xe2\x9c\x97 ", 4); // ✗
        } else {
            write_err("\\ ERR ", 6);
        }
        auto name_colored = ansi::colorize(tool_name, theme_.tool_name, StyleFlag::none, cap_);
        write_err(name_colored.data(), name_colored.size());
        if (!reason.empty()) {
            write_err(" \xe2\x80\x94 ", 4); // —
            auto reason_colored = ansi::colorize(reason, theme_.tool_error_text, StyleFlag::dim, cap_);
            write_err(reason_colored.data(), reason_colored.size());
        }
        write_err("\n", 1);
    }

    // ---- 响应统计 ----

    void on_usage_stats(int prompt_tokens, int completion_tokens,
                        double total_seconds, double ttfb_seconds,
                        bool has_ttfb,
                        std::string_view model_name,
                        int64_t context_length) override {
        finish_thinking();
        finish_text();

        // 格式：── model_name ↑N ↓N latency (ttfb) ctx Xk/Yk Z% ──
        // 格式：──────────────────── model_name ↑N ↓N latency (ttfb) ctx Xk/Yk Z%
        // 性能：全部栈缓冲区格式化，零堆分配

        // 前置横线（20 个 ─）
        {
            constexpr int kSepLen = 20;
            char sep_buf[kSepLen * 3 + 2];
            int pos = 0;
            if (cap_.unicode) {
                for (int i = 0; i < kSepLen; ++i) {
                    sep_buf[pos++] = '\xe2'; sep_buf[pos++] = '\x94'; sep_buf[pos++] = '\x80';
                }
            } else {
                for (int i = 0; i < kSepLen; ++i) {
                    sep_buf[pos++] = '-';
                }
            }
            sep_buf[pos++] = ' ';
            auto sep = ansi::colorize(std::string_view(sep_buf, pos),
                                      theme_.system_info, StyleFlag::dim, cap_);
            write_err(sep.data(), sep.size());
        }

        // 1. 模型名（亮蓝着色）
        if (!model_name.empty()) {
            auto colored = ansi::colorize(model_name, theme_.tool_name, StyleFlag::none, cap_);
            write_err(colored.data(), colored.size());
            write_err(" ", 1);
        }

        // 2. ↑N ↓N latency (ttfb) — dim 着色
        {
            char buf[128];
            int pos = 0;
            auto append = [&](const char* s, int len) {
                if (pos + len < static_cast<int>(sizeof(buf))) {
                    std::memcpy(buf + pos, s, len);
                    pos += len;
                }
            };

            if (prompt_tokens > 0) {
                if (cap_.unicode) append("\xe2\x86\x91", 3); // ↑
                else append("^", 1);
                pos += int_to_buf(buf + pos, sizeof(buf) - pos, prompt_tokens);
                append(" ", 1);
            }
            if (completion_tokens > 0) {
                if (cap_.unicode) append("\xe2\x86\x93", 3); // ↓
                else append("v", 1);
                pos += int_to_buf(buf + pos, sizeof(buf) - pos, completion_tokens);
                append(" ", 1);
            }

            // 延迟
            {
                char tbuf[16];
                format_seconds_buf(total_seconds, tbuf, sizeof(tbuf));
                append(tbuf, static_cast<int>(std::strlen(tbuf)));
            }

            if (has_ttfb && ttfb_seconds > 0) {
                append(" (ttfb ", 7);
                char tbuf[16];
                format_seconds_buf(ttfb_seconds, tbuf, sizeof(tbuf));
                append(tbuf, static_cast<int>(std::strlen(tbuf)));
                append(")", 1);
            }

            auto styled = ansi::colorize(std::string_view(buf, pos),
                                          theme_.system_info, StyleFlag::dim, cap_);
            write_err(styled.data(), styled.size());
        }

        // 3. ctx Xk/Yk Z% — 上下文用量
        if (prompt_tokens > 0 && context_length > 0) {
            write_err(" ", 1);

            // ctx 前缀
            auto ctx_label = ansi::colorize("ctx", theme_.system_info, StyleFlag::dim, cap_);
            write_err(ctx_label.data(), ctx_label.size());
            write_err(" ", 1);

            // Xk/Yk
            {
                char used_buf[16], total_buf[16];
                format_token_count_buf(prompt_tokens, used_buf, sizeof(used_buf));
                format_token_count_buf(static_cast<int64_t>(context_length), total_buf, sizeof(total_buf));
                char ratio_buf[40];
                int rpos = 0;
                auto ulen = static_cast<int>(std::strlen(used_buf));
                std::memcpy(ratio_buf, used_buf, ulen); rpos += ulen;
                ratio_buf[rpos++] = '/';
                auto tlen = static_cast<int>(std::strlen(total_buf));
                std::memcpy(ratio_buf + rpos, total_buf, tlen); rpos += tlen;
                auto ratio_styled = ansi::colorize(std::string_view(ratio_buf, rpos),
                                                   theme_.system_info, StyleFlag::dim, cap_);
                write_err(ratio_styled.data(), ratio_styled.size());
            }

            // Z% — 占比色彩分级
            {
                int pct = static_cast<int>(static_cast<double>(prompt_tokens) * 100.0
                                           / static_cast<double>(context_length));
                if (pct > 999) pct = 999;
                char pct_buf[8];
                int plen = int_to_buf(pct_buf, sizeof(pct_buf), pct);
                pct_buf[plen++] = '%';

                // 色彩分级：<50% 淡绿，50%-80% 淡黄，>80% 淡红
                Color ctx_color;
                if (pct < 50) {
                    ctx_color = Color::from_rgb(0x6A, 0x9F, 0x6A); // 淡绿
                } else if (pct < 80) {
                    ctx_color = Color::from_rgb(0xA8, 0x90, 0x40); // 淡黄
                } else {
                    ctx_color = Color::from_rgb(0xA0, 0x50, 0x50); // 淡红
                }
                auto pct_styled = ansi::colorize(std::string_view(pct_buf, plen),
                                                  ctx_color, StyleFlag::none, cap_);
                write_err(" ", 1);
                write_err(pct_styled.data(), pct_styled.size());
            }
        }

        write_err("\n", 1);
    }

private:
    Theme theme_;
    TerminalCapabilities cap_;
    DisplayConfig config_;
    SyntaxHighlighter highlighter_;
    MarkdownRenderer md_renderer_;
    Spinner spinner_;
    bool in_thinking_;
    bool in_text_;
    bool text_time_printed_;
    bool thinking_need_prefix_;
    bool thinking_color_on_;
    bool thinking_at_line_start_;

    static container::String make_timestamp() {
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        char buf[10];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        return container::String(buf, 8);
    }

    static std::string format_seconds(double seconds) {
        if (seconds < 0.01) return "<0.01s";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
        return buf;
    }

    /// 整数转字符串写入缓冲区，返回写入长度（零堆分配）
    static int int_to_buf(char* buf, size_t bufsize, int64_t value) {
        if (value == 0) { buf[0] = '0'; return 1; }
        char tmp[24];
        int len = 0;
        auto v = value < 0 ? -value : value;
        while (v > 0) { tmp[len++] = '0' + static_cast<char>(v % 10); v /= 10; }
        if (value < 0 && len < static_cast<int>(bufsize) - 1) tmp[len++] = '-';
        // 反转
        for (int i = 0; i < len / 2; ++i) { char t = tmp[i]; tmp[i] = tmp[len-1-i]; tmp[len-1-i] = t; }
        auto copy_len = static_cast<size_t>(len) < bufsize ? static_cast<size_t>(len) : bufsize;
        std::memcpy(buf, tmp, copy_len);
        return static_cast<int>(copy_len);
    }

    /// 延迟格式化写入缓冲区（零堆分配）
    static void format_seconds_buf(double seconds, char* buf, size_t bufsize) {
        if (seconds < 0.01) {
            const char* s = "<0.01s";
            auto slen = std::strlen(s);
            auto copy = slen < bufsize ? slen : bufsize - 1;
            std::memcpy(buf, s, copy);
            buf[copy] = '\0';
            return;
        }
        std::snprintf(buf, bufsize, "%.2fs", seconds);
    }

    /// 人类可读 token 计数：>1024 用 k 后缀（零堆分配）
    static void format_token_count_buf(int64_t tokens, char* buf, size_t bufsize) {
        if (tokens < 1024) {
            int len = int_to_buf(buf, bufsize, tokens);
            buf[len] = '\0';
        } else {
            int k = static_cast<int>(tokens / 1024);
            int len = int_to_buf(buf, bufsize, k);
            if (static_cast<size_t>(len) + 1 < bufsize) {
                buf[len++] = 'k';
            }
            buf[len] = '\0';
        }
    }

    void finish_thinking() {
        if (!in_thinking_) return;
        in_thinking_ = false;
        if (thinking_color_on_) {
            auto reset = ansi::reset();
            if (!reset.empty()) write_err(reset.data(), reset.size());
            thinking_color_on_ = false;
        }
        if (!thinking_at_line_start_) {
            write_err("\n", 1);
        }
        thinking_need_prefix_ = true;
        thinking_at_line_start_ = true;
    }

    void finish_text() {
        if (!in_text_) return;
        in_text_ = false;
        auto remaining = md_renderer_.flush();
        if (!remaining.empty()) write_out(remaining.data(), remaining.size());
        md_renderer_.reset();
    }

    void write_out(const char* data, size_t len) {
        if (len == 0) return;
        fwrite(data, 1, len, stdout);
        fflush(stdout);
    }

    void write_err(const char* data, size_t len) {
        if (len == 0) return;
        fwrite(data, 1, len, stderr);
    }
};

// ============================================================
// 工厂函数
// ============================================================
std::unique_ptr<Renderer> create_terminal_renderer(const Theme& theme,
                                                    const TerminalCapabilities& cap,
                                                    const DisplayConfig& config) {
    return std::make_unique<TerminalRenderer>(theme, cap, config);
}

std::unique_ptr<Renderer> create_silent_renderer() {
    return std::make_unique<SilentRenderer>();
}

}  // namespace ben_gear::cli
