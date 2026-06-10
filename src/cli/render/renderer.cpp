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
    void on_plan_steps(std::string_view) override {}
    void on_step_started(int, int, std::string_view) override {}
    void on_step_completed(int, std::string_view) override {}
    void on_step_skipped(int, std::string_view) override {}
    void on_plan_finished() override {}
    void on_plan_message(std::string_view) override {}
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
// - 不显示独立的 "Assistant" 标签行
// - thinking 标签后附时间：💭 thinking · 14:32:05（中点分隔）
// - 工具名称后附时间：⚡ tool_name · 14:32:05（中点分隔）
// - 正文首个 token 前附时间：──── 14:32:05（横线分隔线，回合视觉锚点）
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

        // 正文首个 token 前输出时间（横线分隔线 + 时间，回合视觉锚点）
        if (!text_time_printed_) {
            text_time_printed_ = true;
            auto ts = make_timestamp();
            // 输出 "──── 14:32:05"
            auto line_colored = ansi::colorize(
                cap_.unicode ? "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 " : "---- ",
                theme_.assistant_hr, StyleFlag::none, cap_);
            write_out(line_colored.data(), line_colored.size());
            auto ts_colored = ansi::colorize(ts, theme_.system_info, StyleFlag::dim, cap_);
            write_out(ts_colored.data(), ts_colored.size());
            write_out("\n", 1);
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

        // 首个 thinking token：停 spinner + 输出 thinking 标识 + 时间
        if (!in_thinking_) {
            spinner_.stop();
            finish_text();
            in_thinking_ = true;
            thinking_need_prefix_ = true;
            thinking_color_on_ = false;
            thinking_at_line_start_ = true;

            // 💭 thinking · 14:32:05
            {
                auto dim_code = ansi::dim();
                if (!dim_code.empty()) write_err(dim_code.data(), dim_code.size());
                auto label_color = ansi::fg(theme_.thinking_label, cap_);
                if (!label_color.empty()) write_err(label_color.data(), label_color.size());
                auto bold_code = ansi::bold();
                if (!bold_code.empty()) write_err(bold_code.data(), bold_code.size());
                if (cap_.unicode) {
                    write_err("\xf0\x9f\x92\xad ", 5);  // 💭
                }
                write_err("thinking ", 9);
                // 中点分隔符 + 时间
                if (cap_.unicode) {
                    write_err("\xc2\xb7 ", 3);  // ·
                } else {
                    write_err("- ", 2);
                }
                if (!bold_code.empty()) write_err(bold_code.data(), bold_code.size());
                auto ts = make_timestamp();
                auto ts_colored = ansi::colorize(ts, theme_.system_info, StyleFlag::dim, cap_);
                write_err(ts_colored.data(), ts_colored.size());
                auto reset = ansi::reset();
                if (!reset.empty()) write_err(reset.data(), reset.size());
            }
            write_err("\n", 1);
        }

        // 逐 token 即时输出，遇 \n 切换到下一行
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
                    // 行首缩进：2 空格
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
        auto colored = ansi::colorize(message, theme_.system_info, StyleFlag::dim, cap_);
        write_err(colored.data(), colored.size());
        write_err("\n", 1);
    }

    void on_tool_call(std::string_view id, std::string_view name, std::string_view args_json) override {
        spinner_.stop();
        finish_thinking();
        finish_text();

        // ┌ ⚡ tool_name 14:32:05
        if (cap_.unicode) {
            write_err("\xe2\x94\x8c ", 4);  // ┌
        } else {
            write_err("[ ", 2);
        }
        if (cap_.unicode) {
            write_err("\xe2\x9a\xa1 ", 4);  // ⚡
        } else {
            write_err("> ", 2);
        }
        auto name_colored = ansi::colorize(name, theme_.tool_name, StyleFlag::bold, cap_);
        write_err(name_colored.data(), name_colored.size());

        // 工具名称后附时间（中点分隔）
        {
            if (cap_.unicode) {
                write_err(" \xc2\xb7 ", 4);  //  · 
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
                write_err("\xe2\x94\x82 ", 4);  // │
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
            write_err("\xe2\x94\x94 ", 4);  // └
        } else {
            write_err("\\ ", 2);
        }

        if (success) {
            if (cap_.unicode) {
                write_err("\xe2\x9c\x93 ", 4);  // ✓
            } else {
                write_err("OK ", 3);
            }
            auto marker = ansi::colorize("ok", theme_.tool_success_marker, StyleFlag::none, cap_);
            write_err(marker.data(), marker.size());
        } else {
            if (cap_.unicode) {
                write_err("\xe2\x9c\x97 ", 4);  // ✗
            } else {
                write_err("ERR ", 4);
            }
            auto marker = ansi::colorize("error", theme_.tool_error_marker, StyleFlag::none, cap_);
            write_err(marker.data(), marker.size());
        }

        {
            base::container::String size_str("  ");
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
            write_err("  ", 2);
            auto err_text = ansi::colorize(output, theme_.tool_error_text, StyleFlag::none, cap_);
            write_err(err_text.data(), err_text.size());
        }

        write_err("\n", 1);
    }

    // ---- 计划模式 ----

    void on_plan_steps(std::string_view steps_text) override {
        auto label = ansi::colorize("Plan", theme_.assistant_heading_h2, StyleFlag::bold, cap_);
        write_out(label.data(), label.size());
        write_out("\n", 1);
        write_out(steps_text.data(), steps_text.size());
        write_out("\n", 1);
    }

    void on_step_started(int step_index, int total, std::string_view description) override {
        // ▶ Step 2/5: description
        char prefix[64];
        int len = 0;
        if (cap_.unicode) {
            prefix[0] = (char)0xe2; prefix[1] = (char)0x96; prefix[2] = (char)0xb6; len = 3;
        } else {
            prefix[0] = '>'; len = 1;
        }
        prefix[len++] = ' ';
        auto step_label = ansi::colorize(
            std::string_view(prefix, len), theme_.assistant_heading_h2, StyleFlag::none, cap_);
        write_out(step_label.data(), step_label.size());

        char step_str[32];
        int slen = snprintf(step_str, sizeof(step_str), "Step %d/%d: ", step_index, total);
        auto step_colored = ansi::colorize(
            std::string_view(step_str, slen), theme_.system_info, StyleFlag::bold, cap_);
        write_out(step_colored.data(), step_colored.size());

        auto desc_colored = ansi::colorize(description, theme_.assistant_text, StyleFlag::none, cap_);
        write_out(desc_colored.data(), desc_colored.size());
        write_out("\n", 1);
    }

    void on_step_completed(int /*step_index*/, std::string_view result) override {
        if (!result.empty()) {
            auto dim = ansi::colorize(result, theme_.system_info, StyleFlag::dim, cap_);
            write_out(dim.data(), dim.size());
            write_out("\n", 1);
        }
    }

    void on_step_skipped(int step_index, std::string_view description) override {
        char buf[64];
        int len = 0;
        if (cap_.unicode) {
            buf[0] = (char)0xe2; buf[1] = (char)0x8a; buf[2] = (char)0x98; len = 3;
        }
        len += snprintf(buf + len, sizeof(buf) - len, " Step %d skipped", step_index);
        auto msg = ansi::colorize(std::string_view(buf, len), theme_.system_info, StyleFlag::dim, cap_);
        write_out(msg.data(), msg.size());
        if (!description.empty()) {
            write_out(": ", 2);
            auto desc = ansi::colorize(description, theme_.system_info, StyleFlag::dim, cap_);
            write_out(desc.data(), desc.size());
        }
        write_out("\n", 1);
    }

    void on_plan_finished() override {
        const char* fin = cap_.unicode ? "\xe2\x9c\x85 Plan completed" : "Plan completed";
        auto msg = ansi::colorize(fin, theme_.assistant_heading_h2, StyleFlag::bold, cap_);
        write_out(msg.data(), msg.size());
        write_out("\n", 1);
    }

    void on_plan_message(std::string_view message) override {
        auto msg = ansi::colorize(message, theme_.system_info, StyleFlag::none, cap_);
        write_out(msg.data(), msg.size());
        write_out("\n", 1);
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
    bool text_time_printed_;  // 正文时间是否已输出
    bool thinking_need_prefix_;
    bool thinking_color_on_;
    bool thinking_at_line_start_;

    /// 生成当前时间字符串 "HH:MM:SS"
    static container::String make_timestamp() {
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        char buf[10];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        return container::String(buf, 8);
    }

    /// 结束 thinking 区块
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

    /// 结束正文区块
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
