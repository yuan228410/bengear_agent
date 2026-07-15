#include "cli/render/cli_app.hpp"
#include "cli/render/agent_event_sink_adapter.hpp"
#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"

namespace ben_gear::cli {

// ============================================================
// CliApp 实现
// ============================================================
CliApp::CliApp(std::unique_ptr<Renderer> renderer, const DisplayConfig& config)
    : renderer_(std::move(renderer)), display_config_(config) {
    event_sink_ = make_agent_event_sink_adapter(
        *renderer_, display_config_,
        std::string_view(config.model_name.data(), config.model_name.size()),
        config.context_length);
}

CliApp::~CliApp() = default;

std::unique_ptr<CliApp> CliApp::create(const DisplayConfig& display_config,
                                        std::string_view model_name,
                                        int64_t context_length) {
    auto cap = cli::detect_terminal();
    auto theme = Theme::default_dark();
    auto renderer = create_terminal_renderer(theme, cap, display_config);
    // 将模型信息写入 DisplayConfig 以便 CliApp 构造时传递
    auto cfg = display_config;
    if (!model_name.empty()) {
        cfg.model_name = std::string(model_name);
    }
    if (context_length > 0) {
        cfg.context_length = context_length;
    }
    return std::unique_ptr<CliApp>(new CliApp(std::move(renderer), cfg));
}

void CliApp::response_start() {
    renderer_->on_response_start();
}

void CliApp::response_end() {
    renderer_->on_response_end();
}

} // namespace ben_gear::cli
