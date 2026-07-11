#include "cli/render/agent_event_sink_adapter.hpp"

#include "cli/render/renderer.hpp"
#include "cli/render/render_event.hpp"
#include "orchestration/event.hpp"
#include "tool/types.hpp"

namespace ben_gear::cli {
namespace {

class RendererAgentEventSink final : public agent::AgentEventSink {
public:
    RendererAgentEventSink(Renderer& renderer, DisplayConfig config,
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

    void on_tool_call(const llm::ToolCallRequest& call) const override {
        if (!config_.show_tool_call) return;

        base::container::String args;
        if (config_.show_tool_args) {
            args = call.arguments.dump(2);
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

    void on_mode_changed(PlanManager::Mode mode) const override {
        renderer_.on_mode_changed(mode);
    }

    void on_tool_blocked(std::string_view tool_name, std::string_view reason) const override {
        renderer_.on_tool_blocked(tool_name, reason);
    }

    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view /*model_name*/,
                           int64_t /*context_length*/) const override {
        renderer_.on_usage_stats(usage.prompt_tokens, usage.completion_tokens,
                                 latency.total_seconds, latency.ttfb_seconds,
                                 latency.has_ttfb,
                                 std::string_view(model_name_.data(), model_name_.size()),
                                 context_length_);
    }

    void on_execution_event(const orchestration::ExecutionEvent& event) const override {
        renderer_.on_execution_event(to_render_event(event));
    }

    static RenderExecutionEvent to_render_event(const orchestration::ExecutionEvent& event) {
        RenderExecutionEvent out;
        switch (event.kind) {
        case orchestration::ExecutionKind::sub_agent:
            out.kind = RenderExecutionKind::sub_agent;
            break;
        default:
            out.kind = RenderExecutionKind::unknown;
            break;
        }

        switch (event.type) {
        case orchestration::ExecutionEventType::started:
            out.type = RenderExecutionEventType::started;
            break;
        case orchestration::ExecutionEventType::tool_call:
            out.type = RenderExecutionEventType::tool_call;
            break;
        case orchestration::ExecutionEventType::tool_result:
            out.type = RenderExecutionEventType::tool_result;
            break;
        case orchestration::ExecutionEventType::token:
            out.type = RenderExecutionEventType::token;
            break;
        case orchestration::ExecutionEventType::completed:
            out.type = RenderExecutionEventType::completed;
            break;
        case orchestration::ExecutionEventType::failed:
            out.type = RenderExecutionEventType::failed;
            break;
        case orchestration::ExecutionEventType::cancelled:
            out.type = RenderExecutionEventType::cancelled;
            break;
        case orchestration::ExecutionEventType::timeout:
            out.type = RenderExecutionEventType::timeout;
            break;
        default:
            out.type = RenderExecutionEventType::unknown;
            break;
        }

        auto copy_view = [](std::string_view value) {
            return base::container::String(value.data(), value.size());
        };

        out.message = event.message;
        out.text = copy_view(event.payload.text_view());
        out.tool_name = copy_view(event.payload.field_view(orchestration::execution_field::tool_name));
        out.index = copy_view(event.payload.field_view(orchestration::execution_field::index));
        out.total = copy_view(event.payload.field_view(orchestration::execution_field::total));
        out.tool_steps = copy_view(event.payload.field_view(orchestration::execution_field::tool_steps));
        out.was_summarized = event.payload.field_bool(orchestration::execution_field::was_summarized);
        out.was_truncated = event.payload.field_bool(orchestration::execution_field::was_truncated);
        out.total_seconds = event.latency.total_seconds;
        out.prompt_tokens = event.usage.prompt_tokens;
        out.completion_tokens = event.usage.completion_tokens;
        out.total_tokens = event.usage.total_tokens;
        return out;
    }

private:
    Renderer& renderer_;
    DisplayConfig config_;
    base::container::String model_name_;
    int64_t context_length_;
};

}  // namespace

std::unique_ptr<agent::AgentEventSink> make_agent_event_sink_adapter(
    Renderer& renderer,
    DisplayConfig config,
    std::string_view model_name,
    int64_t context_length) {
    return std::make_unique<RendererAgentEventSink>(renderer, std::move(config), model_name, context_length);
}

}  // namespace ben_gear::cli
