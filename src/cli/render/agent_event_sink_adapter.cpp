#include "cli/render/agent_event_sink_adapter.hpp"

#include "cli/render/renderer.hpp"
#include "cli/render/render_event.hpp"
#include "orchestration/event.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/usage.hpp"

namespace ben_gear::cli {

class RendererSinks final : public agent::StreamEventSink,
                             public agent::ToolEventSink,
                             public agent::OrchestrationEventSink,
                             public agent::SubAgentEventSink {
public:
    RendererSinks(Renderer& renderer, DisplayConfig config,
                  std::string_view model_name, int64_t context_length)
        : renderer_(renderer), config_(std::move(config)),
          model_name_(model_name), context_length_(context_length) {}

    void on_token(std::string_view token) const override {
        renderer_.on_assistant_text(token);
    }

    void on_thinking(std::string_view token) const override {
        if (!config_.show_thinking) return;
        renderer_.on_thinking(token);
    }

    void on_tool_call(const acp::ToolCallRequest& call) const override {
        if (!config_.show_tool_call) return;
        std::string args;
        if (config_.show_tool_args) args = call.arguments.dump(2);
        renderer_.on_tool_call(
            std::string_view(call.id.data(), call.id.size()),
            std::string_view(call.name.data(), call.name.size()),
            std::string_view(args.data(), args.size()));
    }

    void on_tool_result(const acp::ToolCallResult& result) const override {
        if (!config_.show_tool_result) return;
        std::string output;
        auto raw = std::string_view(result.output.data(), result.output.size());
        if (config_.tool_result_max_length > 0 &&
            raw.size() > static_cast<size_t>(config_.tool_result_max_length)) {
            output = std::string(raw.data(), config_.tool_result_max_length) + "...";
        } else {
            output = std::string(raw);
        }
        renderer_.on_tool_result(
            std::string_view(result.tool_call_id.data(), result.tool_call_id.size()),
            std::string_view(result.name.data(), result.name.size()),
            result.success,
            std::string_view(output.data(), output.size()),
            result.output.size());
    }

    void on_tool_blocked(std::string_view tool_name, std::string_view reason) const override {
        renderer_.on_tool_blocked(tool_name, reason);
    }

    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view, int64_t) const override {
        renderer_.on_usage_stats(usage.prompt_tokens, usage.completion_tokens,
                                 latency.total_seconds, latency.ttfb_seconds,
                                 latency.has_ttfb, model_name_, context_length_);
    }

    void on_execution_event(const orchestration::ExecutionEvent&) const override {}
    void on_todo_update(const orchestration::TodoItem&, std::string_view) const override {}

    void on_sub_agent_start(const std::string&, const std::string&) const override {}
    void on_sub_agent_progress(const std::string&, const std::string&) const override {}
    void on_sub_agent_complete(const std::string&, const std::string&) const override {}
    void on_sub_agent_error(const std::string&, const std::string&) const override {}

private:
    Renderer& renderer_;
    DisplayConfig config_;
    std::string model_name_;
    int64_t context_length_;
};

std::unique_ptr<agent::StreamEventSink> make_renderer_sinks(
    Renderer& renderer, DisplayConfig config,
    std::string_view model_name, int64_t context_length) {
    return std::make_unique<RendererSinks>(renderer, std::move(config), model_name, context_length);
}

agent::AgentEventSinks make_sinks_from_storage(agent::StreamEventSink& storage) {
    auto& r = static_cast<RendererSinks&>(storage);
    return {r, r, r, r};
}

} // namespace ben_gear::cli
