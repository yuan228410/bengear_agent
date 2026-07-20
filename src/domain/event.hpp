#pragma once

#include "base/utils/json.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ben_gear::acp {
struct ToolCallRequest;
struct ToolCallResult;
} // namespace ben_gear::acp

namespace ben_gear::llm {
struct TokenUsage;
struct RequestLatency;
} // namespace ben_gear::llm

namespace ben_gear::domain {


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

/// Serialized tool call request (JSON string).
/// Tagged wrapper to disambiguate from plain std::string in the variant.
struct ToolCallPayload {
    std::string json;
};

/// Serialized tool call result (JSON string).
struct ToolResultPayload {
    std::string json;
};

/// Domain-level token usage (decoupled from llm layer).
/// The domain layer stores its own representation
/// so it does not depend on llm/usage.hpp.
struct TokenUsage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

using EventPayload = std::variant<
    std::monostate,
    std::string,
    Json,
    ToolCallPayload,
    ToolResultPayload,
    TokenUsage
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

    template <class K, class V>
    void set_field(K&& key, V&& value) {
        fields_.emplace(std::string(std::forward<K>(key)),
                        std::string(std::forward<V>(value)));
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

    template <class T>
    void set_source(T&& value) { source_.assign(std::forward<T>(value)); }

    template <class T>
    void set_type(T&& value) { type_.assign(std::forward<T>(value)); }

    template <class T>
    void set_status(T&& value) { status_.assign(std::forward<T>(value)); }

    template <class T>
    void set_message(T&& value) { message_.assign(std::forward<T>(value)); }

    static DomainEvent make(std::string_view source,
                            std::string_view type,
                            EventPayload payload = {},
                            std::string_view message = {});

    static DomainEvent token(std::string_view text);
    static DomainEvent tool_call(const acp::ToolCallRequest& call);
    static DomainEvent thinking(std::string_view text);
    static DomainEvent tool_result(const acp::ToolCallResult& result);
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
