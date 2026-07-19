#pragma once

#include "cli/render/renderer.hpp"
#include "cli/render/display_config.hpp"
#include "cli/render/agent_event_sink_adapter.hpp"
#include "base/core/event_bus.hpp"
#include <memory>

namespace ben_gear::cli {

/// CLI 应用封装
class CliApp {
public:
    static std::unique_ptr<CliApp> create(const DisplayConfig& display_config = {},
                                          std::string_view model_name = {},
                                          int64_t context_length = 0);

    /// 将 Renderer 连接到 EventBus（在运行会话前调用）
    void connect_to_event_bus(base::EventBus& event_bus);

    void response_start();
    void response_end();

    Renderer& renderer() { return *renderer_; }
    const DisplayConfig& display_config() const { return display_config_; }

    ~CliApp();

private:
    CliApp(std::unique_ptr<Renderer> renderer, const DisplayConfig& config);

    std::unique_ptr<Renderer> renderer_;
    DisplayConfig display_config_;
    EventBusConnection event_bus_conn_;
};

} // namespace ben_gear::cli
