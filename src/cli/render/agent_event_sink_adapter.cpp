#include "cli/render/agent_event_sink_adapter.hpp"

#include "cli/render/renderer.hpp"
#include "agent/core/events.hpp"

namespace ben_gear::cli {

EventBusConnection connect_renderer_to_event_bus(
    Renderer& renderer,
    base::EventBus& event_bus,
    DisplayConfig config,
    std::string_view model_name,
    int64_t context_length) {

    EventBusConnection conn;

    // 文本 token
    conn.token_sub = event_bus.subscribe<agent::TokenEvent>(
        [&renderer](const agent::TokenEvent& e) {
            // CLI 不追踪 usage（ResponseStatsEvent 提供完整统计）
            if (!e.token.empty()) {
                renderer.on_assistant_text(e.token);
                if (e.is_end) {
                    renderer.on_stream_progress(e.cumulative_tokens, nullptr);
                }
            }
        });

    // 思考过程
    conn.thinking_sub = event_bus.subscribe<agent::ThinkingEvent>(
        [&renderer, config](const agent::ThinkingEvent& e) {
            if (config.show_thinking) {
                renderer.on_thinking(e.token);
            }
        });

    // 工具调用（opaque name + args_json）
    conn.tool_call_sub = event_bus.subscribe<agent::ToolCallEvent>(
        [&renderer, config](const agent::ToolCallEvent& e) {
            if (!config.show_tool_call) return;
            renderer.on_tool_call(
                e.tool_call_id,
                e.name,
                e.args_json);
        });

    // 工具结果（opaque output）
    conn.tool_result_sub = event_bus.subscribe<agent::ToolResultEvent>(
        [&renderer, config](const agent::ToolResultEvent& e) {
            if (!config.show_tool_result) return;
            std::string output = e.output;
            if (config.tool_result_max_length > 0 &&
                output.size() > static_cast<size_t>(config.tool_result_max_length)) {
                output.resize(config.tool_result_max_length);
                output += "...";
            }
            renderer.on_tool_result(e.tool_call_id, e.name, e.ok,
                                    output, e.output.size());
        });

    // 工具被拦截
    conn.tool_blocked_sub = event_bus.subscribe<agent::ToolBlockedEvent>(
        [&renderer](const agent::ToolBlockedEvent& e) {
            renderer.on_tool_blocked(e.tool_name, e.reason);
        });

    // 统计信息（使用 opaque 字段，不依赖 llm::TokenUsage）
    conn.stats_sub = event_bus.subscribe<agent::ResponseStatsEvent>(
        [&renderer, model_name_str = std::string(model_name), context_length](
            const agent::ResponseStatsEvent& e) {
            renderer.on_usage_stats(
                static_cast<int>(e.prompt_tokens), static_cast<int>(e.completion_tokens),
                e.total_seconds, e.ttfb_seconds,
                e.has_ttfb, model_name_str, context_length);
        });

    return conn;
}

} // namespace ben_gear::cli