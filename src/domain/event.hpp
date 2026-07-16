#pragma once

#include <unordered_map>
#include <vector>
#include "base/utils/json.hpp"
#include "llm/usage.hpp"
#include "capabilities/tool/types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ben_gear::domain {

namespace container = base::container;

using EventId = std::string;
using TraceId = std::string;
using ParentEventId = std::string;
using EntityId = std::string;
using EventFields = std::unordered_map<std::string, std::string>;

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
    std::string,
    std::unique_ptr<Json>,
    std::unique_ptr<capabilities::tool::ToolCallRequest>,
    std::unique_ptr<capabilities::tool::ToolCallResult>,
    llm::TokenUsage
>;

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
    EventPayload payload;
    TimePoint timestamp = Clock::now();
    uint64_t timestamp_ms = 0;
    uint64_t sequence = 0;

    const EventFields& fields_view() const noexcept {
        return fields_;
    }

    void set_field(std::string_view key, std::string_view value) {
        fields_[std::string(key.data(), key.size())] = std::string(value.data(), value.size());
    }

    void set_field(std::string_view key, const char* value) {
        set_field(key, std::string_view(value ? value : ""));
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

    void set_field(const char* key, std::string value) {
        set_field(std::string_view(key ? key : ""), std::move(value));
    }

    void set_field(std::string_view key, std::string value) {
        fields_[std::string(key.data(), key.size())] = std::move(value);
    }

    void set_field(std::string key, std::string value) {
        fields_[std::move(key)] = std::move(value);
    }

    std::string_view field_view(std::string_view key) const {
        const auto it = fields_.find(std::string(key.data(), key.size()));
        if (it == fields_.end()) {
            return {};
        }
        return std::string_view(it->second);
    }

    std::string_view source_view() const noexcept {
        return source_;
    }

    std::string_view type_view() const noexcept {
        return type_;
    }

    std::string_view status_view() const noexcept {
        return status_;
    }

    std::string_view message_view() const noexcept {
        return message_;
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

    void set_source(std::string_view value) {
        source_ = value;
    }

    void set_source(std::string value) {
        source_ = std::move(value);
    }

    void set_type(std::string_view value) {
        type_ = value;
    }

    void set_type(std::string value) {
        type_ = std::move(value);
    }

    void set_status(std::string_view value) {
        status_ = value;
    }

    void set_status(std::string value) {
        status_ = std::move(value);
    }

    void set_message(std::string_view value) {
        message_ = value;
    }

    void set_message(std::string value) {
        message_ = std::move(value);
    }

    static DomainEvent make(std::string_view source,
                            std::string_view type,
                            EventPayload payload = {},
                            std::string_view message = {});

    static DomainEvent token(std::string_view text);
    static DomainEvent thinking(std::string_view text);
    static DomainEvent tool_call(const capabilities::tool::ToolCallRequest& call);
    static DomainEvent tool_result(const capabilities::tool::ToolCallResult& result);
    static DomainEvent mode_changed(std::string mode);
    static DomainEvent tool_blocked(std::string tool_name, std::string reason);
    static DomainEvent usage(const llm::TokenUsage& usage,
                             const llm::RequestLatency& latency,
                             std::string model_name = {},
                             int64_t context_length = 0);

private:
    std::string source_;      // agent/workflow/tool/memory/server-adapter 等
    std::string type_;        // token/thinking/tool_call/tool_result/mode_changed/...
    std::string status_;      // running/succeeded/failed/cancelled/...
    std::string message_;     // 人类可读摘要；不是 UI 格式
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
