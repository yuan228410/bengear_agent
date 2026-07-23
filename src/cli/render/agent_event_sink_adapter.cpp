#include "cli/render/agent_event_sink_adapter.hpp"

#include "cli/render/renderer.hpp"
#include "agent/core/events.hpp"
#include "acp/types/tool_call_types.hpp"
#include "llm/usage.hpp"

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
            if (e.is_end) {
                renderer.on_stream_progress(e.cumulative_tokens, e.usage);
            } else if (!e.token.empty()) {
                renderer.on_assistant_text(e.token);
                // 每 8 个 token 或有 LLM usage 时报告进度
                if (e.cumulative_tokens % 8 == 0 || e.usage) {
                    renderer.on_stream_progress(e.cumulative_tokens, e.usage);
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

    // 工具调用
    conn.tool_call_sub = event_bus.subscribe<agent::ToolCallEvent>(
        [&renderer, config](const agent::ToolCallEvent& e) {
            if (!config.show_tool_call) return;
            std::string args;
            if (config.show_tool_args) args = e.call.arguments.dump(2);
            renderer.on_tool_call(
                std::string_view(e.call.id.data(), e.call.id.size()),
                std::string_view(e.call.name.data(), e.call.name.size()),
                std::string_view(args.data(), args.size()));
        });

    // 工具结果
    conn.tool_result_sub = event_bus.subscribe<agent::ToolResultEvent>(
        [&renderer, config](const agent::ToolResultEvent& e) {
            if (!config.show_tool_result) return;
            std::string output;
            auto raw = std::string_view(e.result.output.data(), e.result.output.size());
            if (config.tool_result_max_length > 0 &&
                raw.size() > static_cast<size_t>(config.tool_result_max_length)) {
                output = std::string(raw.data(), config.tool_result_max_length) + "...";
            } else {
                output = std::string(raw);
            }
            renderer.on_tool_result(
                std::string_view(e.result.tool_call_id.data(), e.result.tool_call_id.size()),
                std::string_view(e.result.name.data(), e.result.name.size()),
                e.result.success,
                std::string_view(output.data(), output.size()),
                e.result.output.size());
        });

    // 工具被拦截
    conn.tool_blocked_sub = event_bus.subscribe<agent::ToolBlockedEvent>(
        [&renderer](const agent::ToolBlockedEvent& e) {
            renderer.on_tool_blocked(e.tool_name, e.reason);
        });

    // 统计信息
    conn.stats_sub = event_bus.subscribe<agent::ResponseStatsEvent>(
        [&renderer, model_name_str = std::string(model_name), context_length](
            const agent::ResponseStatsEvent& e) {
            renderer.on_usage_stats(
                e.usage.prompt_tokens, e.usage.completion_tokens,
                e.latency.total_seconds, e.latency.ttfb_seconds,
                e.latency.has_ttfb, model_name_str, context_length);
        });

    return conn;
}

} // namespace ben_gear::cli
