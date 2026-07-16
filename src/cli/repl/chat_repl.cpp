#include "cli/repl/chat_repl.hpp"
#include "cli/repl/slash_command_dispatcher.hpp"
#include "cli/render/cli_app.hpp"
#include "cli/render/terminal.hpp"
#include "cli/render/theme.hpp"
#include "cli/render/markdown.hpp"
#include "cli/render/highlight.hpp"

#include "agent/runtime/runtime.hpp"
#include "orchestration/plan.hpp"
#include "workspace/session.hpp"
#include "workspace/history_exporter.hpp"
#include "base/net/cancel.hpp"
#include "base/net/event_loop.hpp"
#include "base/utils/string_utils.hpp"
#include "base/log/logger.hpp"
#include "base/config/settings.hpp"
#include "capabilities/tool/history_tools.hpp"
#include <set>

#include <iostream>
#include <csignal>
#include <ctime>
#include <atomic>

namespace ben_gear {

using namespace cli;
using agent::runtime::Runtime;
using workspace::Session;

/// 构造带着色的提示符字符串及其视觉宽度
/// 格式：bengear HH:MM> （plan mode: bengear🔒 HH:MM>）
/// bengear = bright_green + bold，HH:MM = dim，> = 默认色
/// bengear = bright_green + bold，HH:MM = bright_green，> = bright_green
static std::pair<std::string, int> make_prompt(bool plan_mode) {
    auto cap = cli::detect_terminal();
    auto theme = cli::Theme::default_dark();

    // 时间 HH:MM
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    char time_buf[6];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M", tm);

    std::string prompt;
    prompt.reserve(128);

    // bengear — 亮绿粗体
    auto brand_fg = ansi::fg(theme.user_prompt, cap);
    auto brand_bold = ansi::bold();
    if (!brand_fg.empty()) prompt.append(brand_fg.data(), brand_fg.size());
    if (!brand_bold.empty()) prompt.append(brand_bold.data(), brand_bold.size());
    prompt.append("bengear");

    // 🔒 plan mode 标记
    int display_width = 7; // "bengear" 长度
    if (plan_mode) {
        if (cap.unicode) {
            prompt.append("\xf0\x9f\x94\x92"); // 🔒
            display_width += 1; // Emoji 显示宽度（大部分终端占 2 列但光标按 1 列移动）
        } else {
            prompt.append("[plan]");
            display_width += 6;
        }
    }

    auto reset_code = ansi::reset();
    if (!reset_code.empty()) prompt.append(reset_code.data(), reset_code.size());

    // HH:MM — dim 灰
    // HH:MM — 亮绿（与品牌同色，不加粗）
    prompt.push_back(' ');
    auto time_fg = ansi::fg(theme.user_prompt, cap);
    if (!time_fg.empty()) prompt.append(time_fg.data(), time_fg.size());
    prompt.append(time_buf, 5);
    display_width += 1 + 5; // 空格 + HH:MM
    if (!reset_code.empty()) prompt.append(reset_code.data(), reset_code.size());

    // > — 亮绿
    auto arrow_fg = ansi::fg(theme.user_prompt, cap);
    if (!arrow_fg.empty()) prompt.append(arrow_fg.data(), arrow_fg.size());
    prompt.push_back('>');
    if (!reset_code.empty()) prompt.append(reset_code.data(), reset_code.size());
    display_width += 1;

    // 末尾空格分隔输入
    prompt.push_back(' ');
    display_width += 1;

    return {prompt, display_width};
}


/// ASCII Art banner
static void print_banner(const Runtime& agent, std::string_view session_id = {}, bool is_resumed = false) {
    auto cap = cli::detect_terminal();
    if (!cap.is_tty) return;

    auto& settings = agent.settings();
    auto theme = cli::Theme::default_dark();

    auto ben_color   = theme.assistant_heading_h2;
    auto gear_color  = theme.assistant_heading_h1;
    auto agent_color = theme.hl_function;
    auto dim_color   = theme.system_info;

    static constexpr int kBenEnd   = 20;
    static constexpr int kGearEnd  = 46;

    static const char* kLines[] = {
        "    ____             ______                   ___                    __ ",
        "   / __ )___  ____  / ____/__  ____ ______   /   | ____ ____  ____  / /_",
        "  / __  / _ \\/ __ \\/ / __/ _ \\/ __ `/ ___/  / /| |/ __ `/ _ \\/ __ \\/ __/",
        " / /_/ /  __/ / / / /_/ /  __/ /_/ / /     / ___ / /_/ /  __/ / / / /_  ",
        "/_____/\\___/_/ /_/\\____/\\___/\\__,_/_/     /_/  |_\\__, /\\___/_/ /_/\\__/  ",
    };

    if (!cap.unicode) {
        auto ben   = ansi::colorize("Ben",   ben_color,   StyleFlag::bold, cap);
        auto gear  = ansi::colorize("Gear",  gear_color,  StyleFlag::bold, cap);
        auto ag    = ansi::colorize(" Agent", agent_color, StyleFlag::bold, cap);
        std::cout << " " << ben.c_str() << gear.c_str() << ag.c_str() << "\n";
    } else {
        for (const auto* line : kLines) {
            auto len = static_cast<int>(std::strlen(line));
            auto ben_len   = std::min(kBenEnd,   len);
            auto gear_len  = std::min(kGearEnd,  len);

            std::string_view sv(line, len);
            auto ben_part   = ansi::colorize(sv.substr(0, ben_len),
                                             ben_color, StyleFlag::none, cap);
            auto gear_part  = ansi::colorize(sv.substr(ben_len, gear_len - ben_len),
                                             gear_color, StyleFlag::none, cap);
            auto agent_part = gear_len < len
                ? ansi::colorize(sv.substr(gear_len), agent_color, StyleFlag::none, cap)
                : std::string();

            std::cout << ben_part.c_str() << gear_part.c_str()
                      << agent_part.c_str() << "\n";
        }
    }

    auto provider_str = provider_name(settings.provider);
    auto model_str = settings.model;
    std::string info_line = provider_str + " / " + model_str + "  v" BEN_GEAR_VERSION;
    auto info_colored = ansi::colorize(info_line, dim_color, StyleFlag::dim, cap);
    std::cout << " " << info_colored.c_str() << "\n";

    // 会话信息：恢复最新会话 / 新建会话
    if (!session_id.empty()) {
        std::string session_label = is_resumed ? "恢复最新会话: " : "新建会话: ";
        std::string session_line = session_label + std::string(session_id);
        auto session_colored = ansi::colorize(session_line, dim_color, StyleFlag::dim, cap);
        std::cout << " " << session_colored.c_str() << "\n";
    }

    // Banner 后只留 1 个空行
    std::cout << "\n";
}


ChatRepl::ChatRepl(agent::runtime::Runtime& agent, workspace::Session& session,
                   std::unique_ptr<CliApp> cli_app,
                   Config config)
    : agent_(agent), session_(session), cli_app_(std::move(cli_app)),
      config_(std::move(config)),
      editor_(LineEditor::Config{config_.prompt, {}, config_.enable_history}),
      last_persisted_count_(session_.history().messages().size()) {}

int ChatRepl::run() {
    if (config_.show_banner) {
        auto sid = std::string(session_.session_id().data(), session_.session_id().size());
        print_banner(agent_, sid, config_.is_resumed_session);
    }

    // 注册补全器
    auto completer = std::make_unique<SlashCompleter>(std::vector<SlashCompleter::Command>{
        {"exit", "退出", false},
        {"quit", "退出", false},
        {"help", "显示帮助", false},
        {"new", "创建新会话", false},
        {"sessions", "列出历史会话", false},
        {"history", "历史消息/删除", true},
        {"resume", "恢复历史会话", true},
        {"plan", "计划模式（探索）", true},
        {"compact", "手动上下文压缩", false},
        {"clear", "清屏", false},
        {"model", "显示当前模型", true},
        {"search", "搜索历史消息", true},
        {"export", "导出会话为 Markdown", true},
    });

    // /plan 和 /history 二级子命令补全
    completer->set_sub_provider([](std::string_view cmd) -> std::vector<SlashCompleter::SubCommand> {
        if (cmd == "plan") {
            return {
                {"off", "退出计划模式"},
            };
        }
        if (cmd == "history") {
            return {
                {"delete", "删除历史"},
            };
        }
        return {};
    });

    editor_.set_completer(std::move(completer));

    for (;;) {
        // 根据计划模式动态更新提示符
        auto& pm = agent_.plan_manager();
        auto [prompt_str, prompt_width] = make_prompt(pm.is_active());
        editor_.set_prompt(std::move(prompt_str), prompt_width);

        auto line = editor_.read_line();

        // 非空输入后立即持久化历史，避免退出时丢失
        if (!line.empty() && line != std::string(LineEditor::kInterrupted)) {
            editor_.save_history();
        }

        if (line.empty()) continue;

        if (line == std::string(LineEditor::kInterrupted)) {
            ++interrupt_count_;
            if (interrupt_count_ >= 2) {
                std::cout << "\n";
                return 0;
            }
            continue;
        }
        interrupt_count_ = 0;

        if (line[0] == '/') {
            if (handle_command(line)) continue;
            return 0;
        }

        send_message(line);
    }
}

bool ChatRepl::handle_command(const std::string& line) {
    cli::SlashCommandDispatcher dispatcher({
        agent_,
        session_,
        *cli_app_,
        [this](const std::string& desc) -> bool {
            std::cerr << desc << "\n";
            editor_.set_prompt("确认删除？(Y/n) ", 15);
            auto response = editor_.read_line();
            return response.empty() || response[0] == 'y' || response[0] == 'Y';
        },
    });
    return dispatcher.dispatch(line);
}

bool ChatRepl::send_message(const std::string& prompt) {

    auto& io_loop = agent_.io_context()->loop();
    auto sinks = cli_app_->sinks();

    log::info_fmt("chat request received stream={}", agent_.settings().stream ? "true" : "false");

    net::CancellationToken cancel;
    editor_.suspend_raw_mode();

    static std::atomic<net::CancellationToken*> g_cancel_token{nullptr};
    static std::atomic<net::EventLoop*> g_io_loop{nullptr};
    g_cancel_token.store(&cancel, std::memory_order_release);
    g_io_loop.store(&io_loop, std::memory_order_release);

    auto prev_handler = ::signal(SIGINT, [](int) {
        auto* token = g_cancel_token.load(std::memory_order_acquire);
        if (token) {
            token->cancel();
        }
        auto* loop = g_io_loop.load(std::memory_order_acquire);
        if (loop) {
            auto fd = loop->get_cancel_socket();
            if (fd != net::invalid_socket_handle) {
                loop->close_after(fd, std::chrono::milliseconds(0));
            }
        }
    });

    try {
        // 设置子 Agent 运行时的父回调（桥接到 CLI 渲染）
        // SubAgent 运行时暂未完全实现
        if (false) {
            if (agent_.sub_agent_runtime()) {
                auto event_sink_ptr = std::shared_ptr<domain::EventSink>(
                    nullptr, [](domain::EventSink*) {});
                agent_.sub_agent_runtime()->set_parent_event_sink(event_sink_ptr);
            }
        }
        cli_app_->response_start();
        auto prompt_str = std::string(prompt.data(), prompt.size());
        auto result = net::sync_wait(io_loop,
            agent_.run_session_async({io_loop, session_, std::move(prompt_str), sinks, cancel}));
        cli_app_->response_end();

        // 批量持久化本轮新增消息
        auto& msgs = session_.history().messages();
        auto& db = agent_.history_db();
        auto& ws_name = agent_.workspace_context().workspace_name;
        for (size_t i = last_persisted_count_; i < msgs.size(); ++i) {
            auto& m = msgs[i];
            auto role = m.role();
            if (role == acp::Role::Tool) {
                m.for_each_tool_result([&](const capabilities::tool::ToolCallResult& r) {
                    db.append(ws_name.empty() ? std::string("default") : ws_name,
                              session_.session_id(), std::string("tool"),
                              std::string(r.output.data(), r.output.size()),
                              std::string(r.tool_call_id.data(), r.tool_call_id.size()),
                              std::string(r.name.data(), r.name.size()));
                });
            } else if (role == acp::Role::Assistant) {
                auto text = m.get_all_text();
                auto calls = m.get_tool_calls();
                std::vector<capabilities::tool::ToolCallRequest> std_calls;
                for (auto& c : calls) std_calls.push_back(std::move(c));
                session_.persist_assistant_message(text, std_calls, db);
            } else if (role == acp::Role::User) {
                auto text = m.get_all_text();
                db.append(ws_name.empty() ? std::string("default") : ws_name,
                          session_.session_id(), std::string("user"), text);
            }
        }
        last_persisted_count_ = msgs.size();
        if (result.status < 200 || result.status >= 300) {
            log::error_fmt("request failed status={}", result.status);
            std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
        }
    } catch (const net::OperationCancelled&) {
        std::cerr << "\n[cancelled]\n";
    } catch (const std::exception& e) {
        log::error_fmt("chat error: {}", e.what());
        std::cerr << "error: " << e.what() << "\n";
    }

    // 确保本轮流式写入已完成（async append 可能还在队列中）
    agent_.history_db().flush();

    ::signal(SIGINT, prev_handler);
    g_cancel_token.store(nullptr, std::memory_order_release);
    return true;
}

}  // namespace ben_gear
