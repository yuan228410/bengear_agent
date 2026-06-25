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
