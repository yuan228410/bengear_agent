#pragma once

#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/llm/usage.hpp"
#include "ben_gear/tool/types.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

namespace ben_gear::domain {

namespace container = base::container;

using EventId = container::String;
using TraceId = container::String;
using ParentEventId = container::String;
using EntityId = container::String;
using EventFields = container::Map<container::String, container::String>;
using EventPayload = std::variant<
    std::monostate,
    container::String,
    Json,
    llm::ToolCallRequest,
    llm::ToolCallResult,
    llm::TokenUsage,
    llm::RequestLatency,
    EventFields>;

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/// DomainEvent 是 core 到 UI/API/日志的唯一结构化事件边界。
///
/// 约束：
/// - 不包含 ANSI/Markdown/HTTP/WebSocket/CLI 等展示细节。
/// - 不反向依赖 agent/server/cli/workflow 具体类型。
/// - UI 只做 projection/presenter，不把展示状态写回 domain event。
struct DomainEvent {
    EventId id;
    ParentEventId parent_id;
    TraceId trace_id;
    EntityId entity_id;
    container::String source;      // agent/workflow/tool/memory/server-adapter 等
    container::String type;        // token/thinking/tool_call/tool_result/mode_changed/...
    container::String status;      // running/succeeded/failed/cancelled/...
    container::String message;     // 人类可读摘要；不是 UI 格式
    EventPayload payload;
    EventFields fields;
    TimePoint timestamp = Clock::now();
    uint64_t timestamp_ms = 0;
    uint64_t sequence = 0;

    static DomainEvent make(container::String source,
                            container::String type,
                            EventPayload payload = {},
                            container::String message = {}) {
        DomainEvent event;
        event.source = std::move(source);
        event.type = std::move(type);
        event.payload = std::move(payload);
        event.message = std::move(message);
        event.timestamp = Clock::now();
        return event;
    }

    static DomainEvent token(std::string_view text) {
        return make(container::String("agent"),
                    container::String("token"),
                    container::String(text.data(), text.size()));
    }

    static DomainEvent thinking(std::string_view text) {
        return make(container::String("agent"),
                    container::String("thinking"),
                    container::String(text.data(), text.size()));
    }

    static DomainEvent tool_call(const llm::ToolCallRequest& call) {
        DomainEvent event = make(container::String("tool"),
                                 container::String("tool_call"),
                                 call,
                                 call.name);
        event.entity_id = call.id;
        return event;
    }

    static DomainEvent tool_result(const llm::ToolCallResult& result) {
        DomainEvent event = make(container::String("tool"),
                                 container::String("tool_result"),
                                 result,
                                 result.name);
        event.entity_id = result.tool_call_id;
        event.status = result.success ? container::String("succeeded") : container::String("failed");
        return event;
    }

    static DomainEvent mode_changed(container::String mode) {
        return make(container::String("agent"),
                    container::String("mode_changed"),
                    std::move(mode));
    }

    static DomainEvent tool_blocked(container::String tool_name, container::String reason) {
        DomainEvent event = make(container::String("tool"),
                                 container::String("tool_blocked"),
                                 std::move(reason),
                                 std::move(tool_name));
        event.status = container::String("blocked");
        return event;
    }

    static DomainEvent usage(const llm::TokenUsage& usage,
                             const llm::RequestLatency& latency,
                             container::String model_name = {},
                             int64_t context_length = 0) {
        DomainEvent event = make(container::String("llm"),
                                 container::String("response_stats"),
                                 usage);
        event.fields[container::String("model")] = std::move(model_name);
        event.fields[container::String("context_length")] = container::String(std::to_string(context_length));
        event.fields[container::String("latency_seconds")] = container::String(std::to_string(latency.total_seconds));
        return event;
    }
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_event(const DomainEvent& event) const = 0;
};

class NullEventSink : public EventSink {
public:
    void on_event(const DomainEvent&) const override {}
};

} // namespace ben_gear::domain

namespace ben_gear {
using DomainEvent = domain::DomainEvent;
using EventSink = domain::EventSink;
using NullEventSink = domain::NullEventSink;
}
