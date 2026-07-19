#pragma once

#include "base/core/event_bus.hpp"
#include "base/core/metrics.hpp"
#include "base/core/tracing.hpp"
#include "cli/render/display_config.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::cli {

class Renderer;

/// 将 Renderer 连接到 EventBus，订阅 Agent 事件自动渲染
/// 返回 RAII Subscription 列表，析构时取消订阅
struct EventBusConnection {
    base::Subscription token_sub;
    base::Subscription thinking_sub;
    base::Subscription tool_call_sub;
    base::Subscription tool_result_sub;
    base::Subscription tool_blocked_sub;
    base::Subscription stats_sub;
};

/// 连接 Renderer 到 EventBus，返回所有订阅
EventBusConnection connect_renderer_to_event_bus(
    Renderer& renderer,
    base::EventBus& event_bus,
    DisplayConfig config,
    std::string_view model_name,
    int64_t context_length);

} // namespace ben_gear::cli
