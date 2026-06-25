#pragma once

#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/llm/usage.hpp"
#include "ben_gear/tool/types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
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

namespace event_field {
inline constexpr std::string_view completed = "completed";
inline constexpr std::string_view context_length = "context_length";
inline constexpr std::string_view execution_id = "execution_id";
inline constexpr std::string_view latency_seconds = "latency_seconds";
inline constexpr std::string_view model = "model";
inline constexpr std::string_view progress = "progress";
inline constexpr std::string_view success = "success";
inline constexpr std::string_view task_id = "task_id";
inline constexpr std::string_view task_name = "task_name";
inline constexpr std::string_view total = "total";
inline constexpr std::string_view workflow_id = "workflow_id";
inline constexpr std::string_view workflow_status = "workflow_status";
} // namespace event_field

namespace event_source {
inline constexpr std::string_view agent = "agent";
inline constexpr std::string_view llm = "llm";
inline constexpr std::string_view tool = "tool";
inline constexpr std::string_view workflow = "workflow";
inline constexpr std::string_view workflow_task = "workflow.task";
} // namespace event_source

namespace event_type {
inline constexpr std::string_view completed = "completed";
inline constexpr std::string_view failed = "failed";
inline constexpr std::string_view mode_changed = "mode_changed";
inline constexpr std::string_view progress = "progress";
inline constexpr std::string_view response_stats = "response_stats";
inline constexpr std::string_view started = "started";
inline constexpr std::string_view thinking = "thinking";
inline constexpr std::string_view token = "token";
inline constexpr std::string_view tool_blocked = "tool_blocked";
inline constexpr std::string_view tool_call = "tool_call";
inline constexpr std::string_view tool_result = "tool_result";
} // namespace event_type

namespace event_status {
inline constexpr std::string_view blocked = "blocked";
inline constexpr std::string_view cancelled = "cancelled";
inline constexpr std::string_view failed = "failed";
inline constexpr std::string_view paused = "paused";
inline constexpr std::string_view running = "running";
inline constexpr std::string_view succeeded = "succeeded";
} // namespace event_status

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
    TimePoint timestamp = Clock::now();
    uint64_t timestamp_ms = 0;
    uint64_t sequence = 0;

    const EventFields& fields_view() const noexcept {
        return fields_;
    }

    void set_field(std::string_view key, std::string_view value) {
        fields_[container::String(key.data(), key.size())] = container::String(value.data(), value.size());
    }

    void set_field(std::string_view key, const char* value) {
        set_field(key, std::string_view(value ? value : ""));
    }

    void set_field(std::string_view key, const std::string& value) {
        set_field(key, std::string_view(value.data(), value.size()));
    }

    void set_field(const char* key, const char* value) {
        set_field(std::string_view(key ? key : ""), std::string_view(value ? value : ""));
    }

    void set_field(const char* key, std::string_view value) {
        set_field(std::string_view(key ? key : ""), value);
    }

    void set_field(const char* key, const std::string& value) {
        set_field(std::string_view(key ? key : ""), std::string_view(value.data(), value.size()));
    }

    void set_field(const char* key, container::String value) {
        set_field(std::string_view(key ? key : ""), std::move(value));
    }

    void set_field(std::string_view key, container::String value) {
        fields_[container::String(key.data(), key.size())] = std::move(value);
    }

    void set_field(container::String key, container::String value) {
        fields_[std::move(key)] = std::move(value);
    }

    std::string_view field_view(std::string_view key) const {
        const auto it = fields_.find(container::String(key.data(), key.size()));
        if (it == fields_.end()) {
            return {};
        }
        return std::string_view(it->second.data(), it->second.size());
    }

    std::string_view source_view() const noexcept {
        return std::string_view(source.data(), source.size());
    }

    std::string_view type_view() const noexcept {
        return std::string_view(type.data(), type.size());
    }

    std::string_view status_view() const noexcept {
        return std::string_view(status.data(), status.size());
    }

    bool source_is(std::string_view expected) const noexcept {
        return source_view() == expected;
    }

    bool type_is(std::string_view expected) const noexcept {
        return type_view() == expected;
    }

    bool status_is(std::string_view expected) const noexcept {
        return status_view() == expected;
    }

    void set_status(std::string_view value) {
        status = container::String(value.data(), value.size());
    }

    void set_status(container::String value) {
        status = std::move(value);
    }

    static DomainEvent make(container::String source,
                            container::String type,
                            EventPayload payload = {},
                            container::String message = {});

    static DomainEvent token(std::string_view text);
    static DomainEvent thinking(std::string_view text);
    static DomainEvent tool_call(const llm::ToolCallRequest& call);
    static DomainEvent tool_result(const llm::ToolCallResult& result);
    static DomainEvent mode_changed(container::String mode);
    static DomainEvent tool_blocked(container::String tool_name, container::String reason);
    static DomainEvent usage(const llm::TokenUsage& usage,
                             const llm::RequestLatency& latency,
                             container::String model_name = {},
                             int64_t context_length = 0);

private:
    EventFields fields_;
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
