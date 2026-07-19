#include "cli/render/cli_app.hpp"
#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"

namespace ben_gear::cli {

CliApp::CliApp(std::unique_ptr<Renderer> renderer, const DisplayConfig& config)
    : renderer_(std::move(renderer)), display_config_(config) {}

CliApp::~CliApp() = default;

void CliApp::connect_to_event_bus(base::EventBus& event_bus) {
    event_bus_conn_ = connect_renderer_to_event_bus(
        *renderer_, event_bus, display_config_,
        std::string_view(display_config_.model_name.data(), display_config_.model_name.size()),
        display_config_.context_length);
}

std::unique_ptr<CliApp> CliApp::create(const DisplayConfig& display_config,
                                        std::string_view model_name,
                                        int64_t context_length) {
    auto cap = cli::detect_terminal();
    auto theme = Theme::default_dark();
    auto renderer = create_terminal_renderer(theme, cap, display_config);
    auto cfg = display_config;
    if (!model_name.empty()) cfg.model_name = std::string(model_name);
    if (context_length > 0) cfg.context_length = context_length;
    return std::unique_ptr<CliApp>(new CliApp(std::move(renderer), cfg));
}

void CliApp::response_start() { renderer_->on_response_start(); }
void CliApp::response_end()   { renderer_->on_response_end(); }

} // namespace ben_gear::cli
