#include "domain/event.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/usage.hpp"
#include <atomic>

namespace ben_gear::domain {
namespace {

std::atomic<uint64_t>& event_sequence_counter() {
    static std::atomic<uint64_t> counter{1};
    return counter;
}

uint64_t current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string copy_string(std::string_view value) {
    return std::string(value);
}

} // namespace

DomainEvent DomainEvent::make(std::string_view source,
                              std::string_view type,
                              EventPayload payload,
                              std::string_view message) {
    DomainEvent event;
    event.set_source(std::move(source));
    event.set_type(std::move(type));
    event.payload = std::move(payload);
    event.set_message(std::move(message));
    event.timestamp = Clock::now();
    event.timestamp_ms = current_timestamp_ms();
    event.sequence = event_sequence_counter().fetch_add(1, std::memory_order_relaxed);
    return event;
}

DomainEvent DomainEvent::token(std::string_view text) {
    return make(copy_string(event_source::agent),
                copy_string(event_type::token),
                std::string(text.data(), text.size()));
}

DomainEvent DomainEvent::thinking(std::string_view text) {
    return make(copy_string(event_source::agent),
                copy_string(event_type::thinking),
                std::string(text.data(), text.size()));
}

DomainEvent DomainEvent::tool_call(const capabilities::tool::ToolCallRequest& call) {
    Json j;
    j["id"] = call.id;
    j["name"] = call.name;
    j["arguments"] = call.arguments;
    DomainEvent event = make(copy_string(event_source::tool),
                             copy_string(event_type::tool_call),
                             ToolCallPayload{j.dump()},
                             call.name);
    event.entity_id = call.id;
    return event;
}

DomainEvent DomainEvent::tool_result(const capabilities::tool::ToolCallResult& result) {
    Json j;
    j["tool_call_id"] = result.tool_call_id;
    j["name"] = result.name;
    j["output"] = result.output;
    j["success"] = result.success;
    DomainEvent event = make(copy_string(event_source::tool),
                             copy_string(event_type::tool_result),
                             ToolResultPayload{j.dump()},
                             result.name);
    event.entity_id = result.tool_call_id;
    event.set_status(result.success ? event_status::succeeded : event_status::failed);
    return event;
}

DomainEvent DomainEvent::mode_changed(std::string mode) {
    return make(copy_string(event_source::agent),
                copy_string(event_type::mode_changed),
                std::move(mode));
}

DomainEvent DomainEvent::tool_blocked(std::string tool_name, std::string reason) {
    DomainEvent event = make(copy_string(event_source::tool),
                             copy_string(event_type::tool_blocked),
                             std::move(reason),
                             std::move(tool_name));
    event.set_status(event_status::blocked);
    return event;
}

DomainEvent DomainEvent::usage(const llm::TokenUsage& usage,
                               const llm::RequestLatency& latency,
                               std::string model_name,
                               int64_t context_length) {
    TokenUsage du;
    du.prompt_tokens = usage.prompt_tokens;
    du.completion_tokens = usage.completion_tokens;
    du.total_tokens = usage.total_tokens;
    DomainEvent event = make(copy_string(event_source::llm),
                             copy_string(event_type::response_stats),
                             du);
    event.set_field(event_field::model, std::move(model_name));
    event.set_field(event_field::context_length, std::to_string(context_length));
    event.set_field(event_field::latency_seconds, std::to_string(latency.total_seconds));
    return event;
}

} // namespace ben_gear::domain
