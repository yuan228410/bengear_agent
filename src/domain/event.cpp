#include "ben_gear/domain/event.hpp"

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

container::String to_container_string(std::string_view value) {
    return container::String(value.data(), value.size());
}

} // namespace

DomainEvent DomainEvent::make(container::String source,
                              container::String type,
                              EventPayload payload,
                              container::String message) {
    DomainEvent event;
    event.source = std::move(source);
    event.type = std::move(type);
    event.payload = std::move(payload);
    event.message = std::move(message);
    event.timestamp = Clock::now();
    event.timestamp_ms = current_timestamp_ms();
    event.sequence = event_sequence_counter().fetch_add(1, std::memory_order_relaxed);
    return event;
}

DomainEvent DomainEvent::token(std::string_view text) {
    return make(to_container_string(event_source::agent),
                to_container_string(event_type::token),
                container::String(text.data(), text.size()));
}

DomainEvent DomainEvent::thinking(std::string_view text) {
    return make(to_container_string(event_source::agent),
                to_container_string(event_type::thinking),
                container::String(text.data(), text.size()));
}

DomainEvent DomainEvent::tool_call(const llm::ToolCallRequest& call) {
    DomainEvent event = make(to_container_string(event_source::tool),
                             to_container_string(event_type::tool_call),
                             call,
                             call.name);
    event.entity_id = call.id;
    return event;
}

DomainEvent DomainEvent::tool_result(const llm::ToolCallResult& result) {
    DomainEvent event = make(to_container_string(event_source::tool),
                             to_container_string(event_type::tool_result),
                             result,
                             result.name);
    event.entity_id = result.tool_call_id;
    event.set_status(result.success ? event_status::succeeded : event_status::failed);
    return event;
}

DomainEvent DomainEvent::mode_changed(container::String mode) {
    return make(to_container_string(event_source::agent),
                to_container_string(event_type::mode_changed),
                std::move(mode));
}

DomainEvent DomainEvent::tool_blocked(container::String tool_name, container::String reason) {
    DomainEvent event = make(to_container_string(event_source::tool),
                             to_container_string(event_type::tool_blocked),
                             std::move(reason),
                             std::move(tool_name));
    event.set_status(event_status::blocked);
    return event;
}

DomainEvent DomainEvent::usage(const llm::TokenUsage& usage,
                               const llm::RequestLatency& latency,
                               container::String model_name,
                               int64_t context_length) {
    DomainEvent event = make(to_container_string(event_source::llm),
                             to_container_string(event_type::response_stats),
                             usage);
    event.set_field(event_field::model, std::move(model_name));
    event.set_field(event_field::context_length, std::to_string(context_length));
    event.set_field(event_field::latency_seconds, std::to_string(latency.total_seconds));
    return event;
}

} // namespace ben_gear::domain
