#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/tool/types.hpp"

namespace ben_gear::cli {

// ============================================================
// RichAgentCallbacks — 桥接 Agent 回调 → Renderer
// BenGear 专有，不属于 cli 库核心
// ============================================================
class CliApp::RichAgentCallbacks final : public agent::AgentCallbacks {
public:
    RichAgentCallbacks(Renderer& renderer, DisplayConfig config)
        : renderer_(renderer), config_(std::move(config)) {}

    void on_token(std::string_view token) const override {
        renderer_.on_assistant_text(token);
    }

    void on_thinking(std::string_view token) const override {
        if (!config_.show_thinking) return;
        renderer_.on_thinking(token);
    }

    void on_tool_call(const llm::ToolCallRequest& call) const override {
        if (!config_.show_tool_call) return;

        base::container::String args;
        if (config_.show_tool_args) {
            args = call.arguments.dump();
        }

        renderer_.on_tool_call(
            std::string_view(call.id.data(), call.id.size()),
            std::string_view(call.name.data(), call.name.size()),
            std::string_view(args.data(), args.size()));
    }

    void on_tool_result(const llm::ToolCallResult& result) const override {
        if (!config_.show_tool_result) return;

        base::container::String output;
        if (config_.tool_result_max_length > 0) {
            auto raw = std::string_view(result.output.data(), result.output.size());
            if (raw.size() > static_cast<size_t>(config_.tool_result_max_length)) {
                output = base::container::String(raw.data(), config_.tool_result_max_length);
                output.append("...", 3);
            } else {
                output = base::container::String(raw);
            }
        } else {
            output = base::container::String(result.output.data(), result.output.size());
        }

        renderer_.on_tool_result(
            std::string_view(result.tool_call_id.data(), result.tool_call_id.size()),
            std::string_view(result.name.data(), result.name.size()),
            result.success,
            std::string_view(output.data(), output.size()),
            result.output.size());
    }

private:
    Renderer& renderer_;
    DisplayConfig config_;
};

// ============================================================
// CliApp 实现
// ============================================================
CliApp::CliApp(std::unique_ptr<Renderer> renderer, const DisplayConfig& config)
    : renderer_(std::move(renderer)), display_config_(config) {
    callbacks_ = std::make_unique<RichAgentCallbacks>(*renderer_, display_config_);
}

CliApp::~CliApp() = default;

std::unique_ptr<CliApp> CliApp::create(const DisplayConfig& display_config) {
    auto cap = TerminalCapabilities::detect();
    auto theme = Theme::default_dark();
    auto renderer = create_terminal_renderer(theme, cap, display_config);
    return std::unique_ptr<CliApp>(new CliApp(std::move(renderer), display_config));
}

void CliApp::response_start() {
    renderer_->on_response_start();
}

void CliApp::response_end() {
    renderer_->on_response_end();
}

}  // namespace ben_gear::cli
