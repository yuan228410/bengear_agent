#pragma once

#include "agent/core/event_sink.hpp"
#include "cli/render/display_config.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::cli {

class Renderer;

/// 从 internal storage 指针构建 AgentEventSinks 视图
agent::AgentEventSinks make_sinks_from_storage(agent::StreamEventSink& storage);

/// 创建 Renderer 事件回调（同时实现 Stream/Tool/Orchestration 三个接口）
std::unique_ptr<agent::StreamEventSink> make_renderer_sinks(
    Renderer& renderer,
    DisplayConfig config,
    std::string_view model_name,
    int64_t context_length);

} // namespace ben_gear::cli
