#include "cli/render/renderer.hpp"
#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"
#include "cli/render/highlight.hpp"
#include "cli/render/markdown.hpp"
#include "cli/render/spinner.hpp"
#include "cli/render/display_config.hpp"
#include "llm/usage.hpp"

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
    void on_stream_progress(int, const llm::TokenUsage*) override {}
    void on_assistant_text(std::string_view) override {}
    void on_thinking(std::string_view) override {}
    void on_error(std::string_view) override {}
    void on_system(std::string_view) override {}
    void on_tool_call(std::string_view, std::string_view, std::string_view) override {}
    void on_tool_result(std::string_view, std::string_view, bool, std::string_view, size_t) override {}
    void on_mode_changed(bool) override {}
    void on_tool_blocked(std::string_view, std::string_view) override {}
    void on_usage_stats(int, int, double, double, bool, std::string_view, int64_t) override {}
    void on_execution_event(const RenderExecutionEvent&) override {}
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
          cache_(theme, cap),
          highlighter_(theme_, cap_),
          md_renderer_(theme_, cap_, highlighter_, cache_),
          spinner_(theme_, cap_, cache_),
          in_thinking_(false),
          in_text_(false),
          thinking_color_on_(false),
          thinking_at_line_start_(true),
          think_buf_pos_(0),
          out_flush_counter_(0),
          think_token_counter_(0) {}

    ~TerminalRenderer() override {
        spinner_.stop();
        finish_thinking();
        finish_text();
    }

    void on_response_start() override {
        in_thinking_ = false;
        in_text_ = false;
        thinking_color_on_ = false;
        thinking_at_line_start_ = true;
        think_buf_pos_ = 0;
        stream_tokens_ = 0;
        stream_start_ = std::chrono::steady_clock::now();
        // 启动等待动画
        if (config_.show_spinner) {
            spinner_.start("waiting for response...");
        }
    }

    void on_response_end() override {
        spinner_.stop();
        finish_thinking();
        finish_text();
        flush_out();
        fflush(stderr);
    }

    void on_stream_progress(int cumulative_tokens, const llm::TokenUsage* usage) override {
        stream_tokens_ = cumulative_tokens;
        if (usage) {
            if (usage->prompt_tokens > 0) stream_prompt_tokens_ = usage->prompt_tokens;
            if (usage->completion_tokens > 0) stream_completion_tokens_ = usage->completion_tokens;
        }
    }

    void on_assistant_text(std::string_view token) override {
        if (token.empty()) return;
        spinner_.stop();

        finish_thinking();

        if (!in_text_) {
            in_text_ = true;
            // 首个正文 token 前输出淡色锚点横线（回合视觉分隔）
            {
                constexpr int kAnchorLen = 4;
                char anchor[kAnchorLen * 3 + 1];
                int pos = 0;
                if (cap_.unicode) {
                    for (int i = 0; i < kAnchorLen; ++i) {
                        anchor[pos++] = '\xe2'; anchor[pos++] = '\x94'; anchor[pos++] = '\x80'; // ─
                    }
                } else {
                    for (int i = 0; i < kAnchorLen; ++i) anchor[pos++] = '-';
                }
                anchor[pos++] = ' ';
                auto styled = ansi::colorize(std::string_view(anchor, pos),
                                              theme_.system_info, StyleFlag::dim, cap_);
                write_out(styled.data(), styled.size());
            }
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
            thinking_color_on_ = false;
            thinking_at_line_start_ = true;
            think_buf_pos_ = 0;

            // 💭 thinking · 14:32:05 / 💭 thinking
            {
                auto dim_code = ansi::dim();
                if (!dim_code.empty()) think_write(dim_code.data(), dim_code.size());
                if (config_.show_thinking_label) {
                    auto label_color = ansi::fg(theme_.thinking_label, cap_);
                    if (!label_color.empty()) think_write(label_color.data(), label_color.size());
                    auto bold_code = ansi::bold();
                    if (!bold_code.empty()) think_write(bold_code.data(), bold_code.size());
                    if (cap_.unicode) {
                        think_write("\xf0\x9f\x92\xad ", 5); // 💭
                    }
                    think_write("thinking ", 9);
                    auto reset = ansi::reset();
                    if (!reset.empty()) think_write(reset.data(), reset.size());
                }
            }
            think_write("\n", 1);
        }

        auto dim_code = ansi::dim();
        auto fg_code = ansi::fg(theme_.thinking_text, cap_);

        // 逐字符扫描，但通过栈缓冲批量写入（减少 per-char syscall）
        for (size_t i = 0; i < token.size(); ++i) {
            char c = token[i];
            if (c == '\n') {
                if (thinking_color_on_) {
                    auto reset = ansi::reset();
                    if (!reset.empty()) think_write(reset.data(), reset.size());
                    thinking_color_on_ = false;
                }
                think_write("\n", 1);
                thinking_at_line_start_ = true;
            } else {
                if (thinking_at_line_start_) {
                    think_write("  ", 2);
                    if (!dim_code.empty()) think_write(dim_code.data(), dim_code.size());
                    if (!fg_code.empty()) think_write(fg_code.data(), fg_code.size());
                    thinking_color_on_ = true;
                    thinking_at_line_start_ = false;
                }
                think_write(&c, 1);
            }
        }
        think_flush();
        // 批量刷新：每 kThinkFlushInterval 个 token 才 fflush，减少 syscall
        if (++think_token_counter_ >= kThinkFlushInterval) {
            fflush(stderr);
            think_token_counter_ = 0;
        }
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
            char ts_buf[10];
            make_timestamp(ts_buf, sizeof(ts_buf));
            auto ts_colored = ansi::colorize(std::string_view(ts_buf, 8), theme_.system_info, StyleFlag::dim, cap_);
            write_err(ts_colored.data(), ts_colored.size());
        }

        if (config_.show_tool_id && !id.empty()) {
            write_err(" ", 1);
            auto id_colored = ansi::colorize(id, theme_.tool_args, StyleFlag::dim, cap_);
            write_err(id_colored.data(), id_colored.size());
        }
        write_err("\n", 1);

        if (config_.show_tool_args && !args_json.empty()) {
            // 工具参数逐行输出，每行加 │ 前缀
            auto args_colored = ansi::colorize(args_json, theme_.tool_args, StyleFlag::none, cap_);
            auto prefix = cap_.unicode ? "\xe2\x94\x82 " : "| ";
            size_t pos = 0;
            auto sv = std::string_view(args_colored.data(), args_colored.size());
            while (pos < sv.size()) {
                write_err(prefix, std::strlen(prefix));
                size_t nl = sv.find('\n', pos);
                if (nl == std::string_view::npos) {
                    write_err(sv.data() + pos, sv.size() - pos);
                    break;
                }
                write_err(sv.data() + pos, nl - pos);
                write_err("\n", 1);
                pos = nl + 1;
            }
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
            char size_buf[32];
            int pos = 0;
            size_buf[pos++] = ' ';
            size_t n = output_size;
            if (n < 1024) {
                pos += int_to_buf(size_buf + pos, sizeof(size_buf) - pos, static_cast<int64_t>(n));
                size_buf[pos++] = 'B';
            } else if (n < 1024 * 1024) {
                pos += int_to_buf(size_buf + pos, sizeof(size_buf) - pos, static_cast<int64_t>(n / 1024));
                std::memcpy(size_buf + pos, "KB", 2); pos += 2;
            } else {
                pos += int_to_buf(size_buf + pos, sizeof(size_buf) - pos, static_cast<int64_t>(n / 1024 / 1024));
                std::memcpy(size_buf + pos, "MB", 2); pos += 2;
            }
            auto size_colored = ansi::colorize(std::string_view(size_buf, static_cast<size_t>(pos)),
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

    void on_mode_changed(bool in_plan_mode) override {
        finish_thinking();
        finish_text();
        if (cap_.unicode) {
            if (in_plan_mode) {
                write_err("\xf0\x9f\x94\x92 ", 5); // 🔒
            } else {
                write_err("\xf0\x9f\x94\x93 ", 5); // 🔓
            }
        }
        auto label = in_plan_mode ? "plan mode — tool writes blocked" : "plan mode off";
        auto colored = ansi::colorize(label, theme_.system_info, StyleFlag::dim, cap_);
        write_err(colored.data(), colored.size());
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

        // 更新会话累计
        session_turn_++;
        session_prompt_tokens_ += prompt_tokens;
        session_completion_tokens_ += completion_tokens;

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

        // 2. ↑N ↓N latency (ttfb) — 受 DisplayConfig 控制
        {
            char buf[128];
            int pos = 0;
            auto append = [&](const char* s, int len) {
                if (pos + len < static_cast<int>(sizeof(buf))) {
                    std::memcpy(buf + pos, s, len);
                    pos += len;
                }
            };

            if (config_.show_token_count) {
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
            }

            if (config_.show_timing) {
                char tbuf[16];
                format_seconds_buf(total_seconds, tbuf, sizeof(tbuf));
                append(tbuf, static_cast<int>(std::strlen(tbuf)));

                if (has_ttfb && ttfb_seconds > 0) {
                    append(" (ttfb ", 7);
                    char tbuf2[16];
                    format_seconds_buf(ttfb_seconds, tbuf2, sizeof(tbuf2));
                    append(tbuf2, static_cast<int>(std::strlen(tbuf2)));
                    append(")", 1);
                }
                append(" ", 1);
            }

            if (pos > 0) {
                auto styled = ansi::colorize(std::string_view(buf, pos),
                                              theme_.system_info, StyleFlag::dim, cap_);
                write_err(styled.data(), styled.size());
            }
        }

        // 3. ctx Xk/Yk Z% — 上下文用量（受 show_token_count 控制）
        if (config_.show_token_count && prompt_tokens > 0 && context_length > 0) {
            write_err(" ", 1);

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

            // Z% — 占比色彩分级（<1% 显示一位小数）
            {
                double pct_d = static_cast<double>(prompt_tokens) * 100.0
                              / static_cast<double>(context_length);
                char pct_buf[12];
                int plen;
                if (pct_d < 1.0 && pct_d > 0.0) {
                    // 小于 1% 显示一位小数，如 0.6%
                    int tenth = static_cast<int>(pct_d * 10.0);
                    pct_buf[0] = '0'; pct_buf[1] = '.';
                    pct_buf[2] = '0' + static_cast<char>(tenth);
                    pct_buf[3] = '%'; plen = 4;
                } else {
                    int pct = static_cast<int>(pct_d);
                    if (pct > 999) pct = 999;
                    plen = int_to_buf(pct_buf, sizeof(pct_buf), pct);
                    pct_buf[plen++] = '%';
                }

                Color ctx_color;
                if (pct_d < 50.0) {
                    ctx_color = Color::from_rgb(0x6A, 0x9F, 0x6A);
                } else if (pct_d < 80.0) {
                    ctx_color = Color::from_rgb(0xA8, 0x90, 0x40);
                } else {
                    ctx_color = Color::from_rgb(0xA0, 0x50, 0x50);
                }
                auto pct_styled = ansi::colorize(std::string_view(pct_buf, plen),
                                                  ctx_color, StyleFlag::none, cap_);
                write_err(" ", 1);
                write_err(pct_styled.data(), pct_styled.size());
            }
        }

        // 会话累计（同行尾缀）
        if (config_.show_token_count) {
            auto& dim = cache_.dim;
            auto& sys = cache_.system_info;
            auto& rst = cache_.reset;

            char sbuf[80];
            int spos = 0;
            auto sapp = [&](const char* s, int len) {
                if (spos + len < (int)sizeof(sbuf)) { memcpy(sbuf + spos, s, len); spos += len; }
            };

            sapp("  ", 2);
            if (cap_.unicode) sapp("\xc2\xb7 ", 3); else sapp("- ", 2);  // · 分隔

            if (!dim.empty()) { sapp(dim.data(), dim.size()); }
            if (!sys.empty()) { sapp(sys.data(), sys.size()); }

            sapp("session ", 8);
            if (cap_.unicode) sapp("\xe2\x86\x91", 3); else sapp("^", 1);
            spos += int_to_buf(sbuf + spos, sizeof(sbuf) - spos, session_prompt_tokens_);
            sapp(" ", 1);
            if (cap_.unicode) sapp("\xe2\x86\x93", 3); else sapp("v", 1);
            spos += int_to_buf(sbuf + spos, sizeof(sbuf) - spos, session_completion_tokens_);

            char tbuf[8];
            int tlen = int_to_buf(tbuf, sizeof(tbuf), session_turn_);
            sapp(" ", 1);
            sapp(tbuf, tlen);
            sapp(" turns", 6);

            if (!rst.empty()) { sapp(rst.data(), rst.size()); }

            write_err(sbuf, spos);
        }

        write_err("\n", 1);
    }

private:
    Theme theme_;
    TerminalCapabilities cap_;
    DisplayConfig config_;
    AnsiStyleCache cache_;
    SyntaxHighlighter highlighter_;
    MarkdownRenderer md_renderer_;
    Spinner spinner_;
    bool in_thinking_;
    bool in_text_;
    bool thinking_color_on_;
    bool thinking_at_line_start_;

    // ---- 会话统计 ----
    int session_turn_ = 0;
    int session_prompt_tokens_ = 0;
    int session_completion_tokens_ = 0;

    // ---- 流式状态 ----
    int stream_tokens_ = 0;
    int stream_prompt_tokens_ = 0;   // LLM 返回的实时 input tokens
    int stream_completion_tokens_ = 0;  // LLM 返回的实时 output tokens
    std::chrono::steady_clock::time_point stream_start_;

    /// 时间戳写入栈缓冲区（零堆分配），格式 HH:MM:SS，9 字节含 null
    static void make_timestamp(char* buf, size_t bufsize) {
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        std::strftime(buf, bufsize, "%H:%M:%S", tm);
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

    /// 人类可读 token 计数：<1k 原值，1k-10k 一位小数，≥10k 整数
    static void format_token_count_buf(int64_t tokens, char* buf, size_t bufsize) {
        if (tokens < 1024) {
            int len = int_to_buf(buf, bufsize, tokens);
            buf[len] = '\0';
        } else if (tokens < 10240) {
            // 1k~10k：显示一位小数，如 6.4k
            int whole = static_cast<int>(tokens / 1024);
            int frac = static_cast<int>((tokens % 1024) * 10 / 1024);
            int len = int_to_buf(buf, bufsize, whole);
            buf[len++] = '.';
            buf[len++] = '0' + static_cast<char>(frac);
            if (static_cast<size_t>(len) + 1 < bufsize) buf[len++] = 'k';
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
            if (!reset.empty()) think_write(reset.data(), reset.size());
            thinking_color_on_ = false;
        }
        if (!thinking_at_line_start_) {
            think_write("\n", 1);
        }
        thinking_at_line_start_ = true;
        think_flush();
    }

    void finish_text() {
        if (!in_text_) return;
        in_text_ = false;
        auto remaining = md_renderer_.flush();
        if (!remaining.empty()) write_out(remaining.data(), remaining.size());
        md_renderer_.reset();
    }

    // ---- 思考输出缓冲（减少 per-char syscall） ----
    static constexpr int kThinkBufSize = 4096;
    char think_buf_[kThinkBufSize];
    int think_buf_pos_ = 0;

    // 批量刷新计数器：每 N 个 token 才 fflush，减少 syscall
    static constexpr int kOutFlushInterval = 16;
    static constexpr int kThinkFlushInterval = 8;
    int out_flush_counter_ = 0;
    int think_token_counter_ = 0;

    /// 缓冲写入 stderr（减少 syscall），缓冲满或显式 flush 时才真正 fwrite
    void think_write(const char* data, size_t len) {
        if (len == 0) return;
        if (think_buf_pos_ + static_cast<int>(len) > kThinkBufSize) {
            fwrite(think_buf_, 1, think_buf_pos_, stderr);
            think_buf_pos_ = 0;
            if (len >= kThinkBufSize) {
                fwrite(data, 1, len, stderr);
                return;
            }
        }
        std::memcpy(think_buf_ + think_buf_pos_, data, len);
        think_buf_pos_ += static_cast<int>(len);
    }

    void think_flush() {
        if (think_buf_pos_ > 0) {
            fwrite(think_buf_, 1, think_buf_pos_, stderr);
            think_buf_pos_ = 0;
        }
    }

    // ---- 流式状态行（stderr 底部，实时 token 计数） ----

    void write_out(const char* data, size_t len) {
        if (len == 0) return;
        fwrite(data, 1, len, stdout);
        // 批量刷新：每 kOutFlushInterval 次写入才 fflush，减少 syscall
        if (++out_flush_counter_ >= kOutFlushInterval) {
            fflush(stdout);
            out_flush_counter_ = 0;
        }
    }

    /// 强制刷新 stdout（响应结束、错误等语义边界调用）
    void flush_out() {
        fflush(stdout);
        out_flush_counter_ = 0;
    }

    void write_err(const char* data, size_t len) {
        if (len == 0) return;
        fwrite(data, 1, len, stderr);
    }

    // ---- 子 Agent 事件渲染 ----
    // 树状缩进 + ┌/│/└ 结构，与主 Agent 工具调用风格一致
    // 视觉层次：┌ 🔍 [1/1] 任务描述 → │ ⚡ tool → │ ✓ tool → │ 💬 输出 → └ ✓ done
    // ---- 子 Agent 事件渲染 ----
    // 嵌套树状结构：主 Agent 用 ┌/│/└，子 Agent 用 ╭/┆/╰
    // 视觉层级：│ ╭ sub-agent 🔍 → │ ┆ ⚡ tool → │ ┆ ✓ tool → │ ╰ ✓ done
    void on_execution_event(const RenderExecutionEvent& event) override {
        spinner_.stop();
        // 子 Agent 行前缀：│（外层，与主 Agent │ 对齐）
        auto write_outer = [&]() {
            if (cap_.unicode) write_err("\xe2\x94\x82 ", 4); // │
            else write_err("| ", 2);
        };
        // 子 Agent 内层前缀：┆
        auto write_inner = [&]() {
            if (cap_.unicode) write_err("\xe2\x94\x86 ", 4); // ┆
            else write_err("  ", 2);
        };
        // 子 Agent 闭合前缀：╰
        auto write_close = [&]() {
            if (cap_.unicode) write_err("\xe2\x95\xb0 ", 4); // ╰
            else write_err("+ ", 2);
        };

        const auto kind = event.kind;
        const auto type = event.type;
        if (kind != RenderExecutionKind::sub_agent) {
            return;
        }

        auto text = std::string_view(event.text.data(), event.text.size());
        auto message = std::string_view(event.message.data(), event.message.size());

        switch (type) {
        case RenderExecutionEventType::started: {
            write_outer();
            if (cap_.unicode) write_err("\xe2\x95\xad ", 4); // ╭
            else write_err("+ ", 2);
            auto sub_label = ansi::colorize("sub-agent ", theme_.system_info, StyleFlag::dim, cap_);
            write_err(sub_label.data(), sub_label.size());
            if (cap_.unicode) write_err("\xf0\x9f\x94\x8d ", 5); // 🔍
            else write_err("? ", 2);
            auto index = std::string_view(event.index.data(), event.index.size());
            auto total = std::string_view(event.total.data(), event.total.size());
            if (!index.empty() && !total.empty() && total != "1") {
                auto bracket = ansi::colorize("[" + std::string(index) + "/" + std::string(total) + "] ",
                                              theme_.system_info, StyleFlag::dim, cap_);
                write_err(bracket.data(), bracket.size());
            }
            auto prompt_text = message.empty() ? text : message;
            auto prompt = ansi::colorize(prompt_text, theme_.tool_name, StyleFlag::none, cap_);
            write_err(prompt.data(), prompt.size());
            write_err("\n", 1);
            break;
        }
        case RenderExecutionEventType::tool_call: {
            write_outer();
            write_inner();
            auto icon = ansi::colorize(std::string_view("\xe2\x9a\xa1 ", 5), theme_.tool_name, StyleFlag::none, cap_);
            auto name = ansi::colorize(std::string_view(event.tool_name.data(), event.tool_name.size()), theme_.tool_name, StyleFlag::none, cap_);
            write_err(icon.data(), icon.size());
            write_err(name.data(), name.size());
            write_err("\n", 1);

            if (config_.show_tool_args && !text.empty()) {
                auto args_colored = ansi::colorize(text, theme_.tool_args, StyleFlag::none, cap_);
                auto prefix = cap_.unicode ? "\xe2\x94\x82 \xe2\x94\x86 " : "|   ";
                auto prefix_len = std::strlen(prefix);
                auto sv = std::string_view(args_colored.data(), args_colored.size());
                size_t pos = 0;
                while (pos < sv.size()) {
                    write_err(prefix, prefix_len);
                    size_t nl = sv.find('\n', pos);
                    if (nl == std::string_view::npos) {
                        write_err(sv.data() + pos, sv.size() - pos);
                        write_err("\n", 1);
                        break;
                    }
                    write_err(sv.data() + pos, nl - pos);
                    write_err("\n", 1);
                    pos = nl + 1;
                }
            }
            break;
        }
        case RenderExecutionEventType::tool_result: {
            write_outer();
            write_inner();
            auto icon = ansi::colorize(std::string_view("\xe2\x9c\x93 ", 5), theme_.tool_success_marker, StyleFlag::none, cap_);
            auto name = ansi::colorize(std::string_view(event.tool_name.data(), event.tool_name.size()), theme_.system_info, StyleFlag::dim, cap_);
            write_err(icon.data(), icon.size());
            write_err(name.data(), name.size());
            write_err("\n", 1);
            break;
        }
        case RenderExecutionEventType::token:
            break;
        case RenderExecutionEventType::completed: {
            write_outer();
            write_close();
            auto icon = ansi::colorize(std::string_view("\xe2\x9c\x93 ", 5), theme_.tool_success_marker, StyleFlag::none, cap_);
            write_err(icon.data(), icon.size());
            auto label = ansi::colorize("done", theme_.tool_name, StyleFlag::none, cap_);
            write_err(label.data(), label.size());
            if (cap_.unicode) write_err(" \xc2\xb7 ", 4);
            else write_err(" - ", 3);

            char time_buf[32];
            int time_len = 0;
            double secs = event.total_seconds;
            if (secs < 0.01) time_len = snprintf(time_buf, sizeof(time_buf), "%.0fms", secs * 1000);
            else time_len = snprintf(time_buf, sizeof(time_buf), "%.1fs", secs);
            auto time_colored = ansi::colorize(std::string_view(time_buf, static_cast<size_t>(time_len)),
                                               theme_.system_info, StyleFlag::dim, cap_);
            write_err(time_colored.data(), time_colored.size());

            if (event.total_tokens > 0 || event.prompt_tokens > 0) {
                write_err(" ", 1);
                if (cap_.unicode) write_err("\xe2\x86\x91", 3);
                else write_err("^", 1);
                char ubuf[16];
                int ulen = int_to_buf(ubuf, sizeof(ubuf), event.prompt_tokens);
                write_err(ubuf, static_cast<size_t>(ulen));
                write_err(" ", 1);
                if (cap_.unicode) write_err("\xe2\x86\x93", 3);
                else write_err("v", 1);
                char dbuf[16];
                int dlen = int_to_buf(dbuf, sizeof(dbuf), event.completion_tokens);
                write_err(dbuf, static_cast<size_t>(dlen));
            }

            auto steps = std::string_view(event.tool_steps.data(), event.tool_steps.size());
            if (!steps.empty() && steps != "0") {
                write_err(" ", 1);
                char steps_buf[32];
                int steps_len = snprintf(steps_buf, sizeof(steps_buf), "steps=%.*s", static_cast<int>(steps.size()), steps.data());
                auto steps_colored = ansi::colorize(std::string_view(steps_buf, static_cast<size_t>(steps_len)),
                                                    theme_.system_info, StyleFlag::dim, cap_);
                write_err(steps_colored.data(), steps_colored.size());
            }

            if (event.was_summarized) {
                auto tag = ansi::colorize(" summarized", theme_.system_info, StyleFlag::dim, cap_);
                write_err(tag.data(), tag.size());
            } else if (event.was_truncated) {
                auto tag = ansi::colorize(" truncated", theme_.system_info, StyleFlag::dim, cap_);
                write_err(tag.data(), tag.size());
            }

            write_err("\n", 1);
            break;
        }
        case RenderExecutionEventType::failed: {
            write_outer();
            write_close();
            auto icon = ansi::colorize(std::string_view("\xe2\x9c\x97 ", 5), theme_.error_text, StyleFlag::none, cap_);
            auto error_text = message.empty() ? text : message;
            auto rendered = ansi::colorize(error_text, theme_.error_text, StyleFlag::none, cap_);
            write_err(icon.data(), icon.size());
            write_err(rendered.data(), rendered.size());
            write_err("\n", 1);
            break;
        }
        case RenderExecutionEventType::cancelled:
        case RenderExecutionEventType::timeout: {
            write_outer();
            write_close();
            auto label = type == RenderExecutionEventType::cancelled ? "cancelled" : "timeout";
            auto rendered = ansi::colorize(label, theme_.system_info, StyleFlag::dim, cap_);
            write_err(rendered.data(), rendered.size());
            write_err("\n", 1);
            break;
        }
        default:
            break;
        }
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


