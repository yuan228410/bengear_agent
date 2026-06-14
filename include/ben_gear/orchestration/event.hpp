#pragma once

#include "ben_gear/orchestration/context.hpp"
#include "ben_gear/orchestration/result.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace ben_gear::orchestration {

/// 统一执行事件。事件只携带结构化数据，不包含 UI 展示格式。
struct ExecutionEvent {
    ExecutionId execution_id;
    ParentExecutionId parent_id;
    TraceId trace_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionEventType type = ExecutionEventType::progress;
    ExecutionStatus status = ExecutionStatus::pending;
    container::String message;
    ExecutionValue payload;
    llm::TokenUsage usage;
    llm::RequestLatency latency;
    TimePoint timestamp = Clock::now();
    uint64_t timestamp_ms = current_timestamp_ms();
    uint64_t sequence = next_sequence();

    static uint64_t current_timestamp_ms() {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    static uint64_t next_sequence() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    static ExecutionEvent make_started(ExecutionId id,
                                       ExecutionKind kind,
                                       container::String message = {}) {
        ExecutionEvent event;
        event.execution_id = std::move(id);
        event.kind = kind;
        event.type = ExecutionEventType::started;
        event.status = ExecutionStatus::running;
        event.message = std::move(message);
        event.timestamp = Clock::now();
        return event;
    }

    static ExecutionEvent make_completed(const ExecutionResult& result) {
        ExecutionEvent event;
        event.execution_id = result.execution_id;
        event.parent_id = result.parent_id;
        event.kind = result.kind;
        event.type = ExecutionEventType::completed;
        event.status = ExecutionStatus::succeeded;
        event.payload = result.output;
        event.usage = result.usage;
        event.latency = result.latency;
        event.timestamp = Clock::now();
        return event;
    }

    static ExecutionEvent make_failed(ExecutionId id,
                                      ExecutionKind kind,
                                      container::String error) {
        ExecutionEvent event;
        event.execution_id = std::move(id);
        event.kind = kind;
        event.type = ExecutionEventType::failed;
        event.status = ExecutionStatus::failed;
        event.message = std::move(error);
        event.timestamp = Clock::now();
        return event;
    }

    static ExecutionEvent make_terminal(ExecutionId id,
                                        ExecutionKind kind,
                                        ExecutionStatus status,
                                        ExecutionEventType type,
                                        container::String message = {}) {
        ExecutionEvent event;
        event.execution_id = std::move(id);
        event.kind = kind;
        event.type = type;
        event.status = status;
        event.message = std::move(message);
        event.timestamp = Clock::now();
        return event;
    }
};

inline void emit(const IExecutionObserver* observer, const ExecutionEvent& event) {
    if (!observer) {
        return;
    }
    observer->on_execution_event(event);
}

} // namespace ben_gear::orchestration
