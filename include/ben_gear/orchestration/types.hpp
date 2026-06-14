#pragma once

#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <chrono>
#include <cstdint>

namespace ben_gear::orchestration {

namespace container = base::container;

using ExecutionId = container::String;
using TraceId = container::String;
using ParentExecutionId = container::String;
using Metadata = container::Map<container::String, container::String>;

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/// 执行对象类型。只描述领域语义，不携带 UI 展示信息。
enum class ExecutionKind : uint8_t {
    chat,
    sub_agent,
    workflow,
    task,
    tool,
    approval
};

/// 统一执行状态。workflow/sub-agent/tool 都使用同一套状态语义。
enum class ExecutionStatus : uint8_t {
    pending,
    running,
    succeeded,
    failed,
    cancelled,
    timeout,
    skipped,
    paused
};

/// 执行事件类型。事件用于状态流转和 UI/API 边界序列化。
enum class ExecutionEventType : uint8_t {
    started,
    progress,
    token,
    tool_call,
    tool_result,
    completed,
    failed,
    cancelled,
    timeout,
    skipped,
    paused,
    resumed
};

inline container::String to_string(ExecutionKind kind) {
    switch (kind) {
    case ExecutionKind::chat: return container::String("chat");
    case ExecutionKind::sub_agent: return container::String("sub_agent");
    case ExecutionKind::workflow: return container::String("workflow");
    case ExecutionKind::task: return container::String("task");
    case ExecutionKind::tool: return container::String("tool");
    case ExecutionKind::approval: return container::String("approval");
    }
    return container::String("unknown");
}

inline container::String to_string(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::pending: return container::String("pending");
    case ExecutionStatus::running: return container::String("running");
    case ExecutionStatus::succeeded: return container::String("succeeded");
    case ExecutionStatus::failed: return container::String("failed");
    case ExecutionStatus::cancelled: return container::String("cancelled");
    case ExecutionStatus::timeout: return container::String("timeout");
    case ExecutionStatus::skipped: return container::String("skipped");
    case ExecutionStatus::paused: return container::String("paused");
    }
    return container::String("unknown");
}

inline container::String to_string(ExecutionEventType type) {
    switch (type) {
    case ExecutionEventType::started: return container::String("started");
    case ExecutionEventType::progress: return container::String("progress");
    case ExecutionEventType::token: return container::String("token");
    case ExecutionEventType::tool_call: return container::String("tool_call");
    case ExecutionEventType::tool_result: return container::String("tool_result");
    case ExecutionEventType::completed: return container::String("completed");
    case ExecutionEventType::failed: return container::String("failed");
    case ExecutionEventType::cancelled: return container::String("cancelled");
    case ExecutionEventType::timeout: return container::String("timeout");
    case ExecutionEventType::skipped: return container::String("skipped");
    case ExecutionEventType::paused: return container::String("paused");
    case ExecutionEventType::resumed: return container::String("resumed");
    }
    return container::String("unknown");
}

inline bool is_terminal(ExecutionStatus status) noexcept {
    return status == ExecutionStatus::succeeded ||
           status == ExecutionStatus::failed ||
           status == ExecutionStatus::cancelled ||
           status == ExecutionStatus::timeout ||
           status == ExecutionStatus::skipped;
}

} // namespace ben_gear::orchestration

namespace ben_gear {
using ExecutionId = orchestration::ExecutionId;
using ExecutionKind = orchestration::ExecutionKind;
using ExecutionStatus = orchestration::ExecutionStatus;
using ExecutionEventType = orchestration::ExecutionEventType;
}
