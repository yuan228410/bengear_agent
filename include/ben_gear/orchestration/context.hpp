#pragma once

#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/orchestration/types.hpp"

#include <chrono>
#include <memory>
#include <optional>

namespace ben_gear::orchestration {

struct ExecutionEvent;
struct ExecutionResult;

/// UI 无关事件观察者。CLI/Web/API 只能实现该边界的适配层。
class IExecutionObserver {
public:
    virtual ~IExecutionObserver() = default;
    virtual void on_execution_event(const ExecutionEvent& event) const = 0;
};

/// 执行选项。字段保持轻量，热路径优先移动而非拷贝。
struct ExecutionOptions {
    container::String system_prompt;
    int max_steps = 0;
    std::chrono::milliseconds timeout{0};
    std::optional<TimePoint> deadline;
    container::String model_override;
    container::Vector<container::String> tool_filter;
    int max_retries = 0;
    std::chrono::milliseconds retry_delay{0};
    Metadata metadata;

    bool has_deadline() const noexcept { return deadline.has_value(); }
};

/// 执行上下文。负责跨 sub-agent/workflow/task 传递取消、deadline、trace 和事件出口。
struct ExecutionContext {
    ExecutionId execution_id;
    ParentExecutionId parent_id;
    TraceId trace_id;
    ExecutionOptions options;
    net::CancellationToken cancel;
    const IExecutionObserver* observer = nullptr;

    bool is_cancelled() const { return cancel.is_cancelled(); }

    bool is_deadline_exceeded(TimePoint now = Clock::now()) const {
        return options.deadline.has_value() && now >= *options.deadline;
    }

    bool should_stop(TimePoint now = Clock::now()) const {
        return is_cancelled() || is_deadline_exceeded(now);
    }
};

/// 从 timeout 生成 deadline。timeout<=0 时不设置 deadline。
inline ExecutionOptions with_deadline(ExecutionOptions options, TimePoint now = Clock::now()) {
    if (options.timeout.count() > 0 && !options.deadline.has_value()) {
        options.deadline = now + options.timeout;
    }
    return options;
}

} // namespace ben_gear::orchestration
