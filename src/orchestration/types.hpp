#pragma once

#include <unordered_map>
#include <vector>

#include <chrono>
#include <cstdint>

namespace ben_gear::orchestration {

namespace container = base::container;

using ExecutionId = std::string;
using TraceId = std::string;
using ParentExecutionId = std::string;
using Metadata = std::unordered_map<std::string, std::string>;

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

inline std::string to_string(ExecutionKind kind) {
    switch (kind) {
    case ExecutionKind::chat: return std::string("chat");
    case ExecutionKind::sub_agent: return std::string("sub_agent");
    case ExecutionKind::workflow: return std::string("workflow");
    case ExecutionKind::task: return std::string("task");
    case ExecutionKind::tool: return std::string("tool");
    case ExecutionKind::approval: return std::string("approval");
    }
    return std::string("unknown");
}

inline std::string to_string(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::pending: return std::string("pending");
    case ExecutionStatus::running: return std::string("running");
    case ExecutionStatus::succeeded: return std::string("succeeded");
    case ExecutionStatus::failed: return std::string("failed");
    case ExecutionStatus::cancelled: return std::string("cancelled");
    case ExecutionStatus::timeout: return std::string("timeout");
    case ExecutionStatus::skipped: return std::string("skipped");
    case ExecutionStatus::paused: return std::string("paused");
    }
    return std::string("unknown");
}

inline std::string to_string(ExecutionEventType type) {
    switch (type) {
    case ExecutionEventType::started: return std::string("started");
    case ExecutionEventType::progress: return std::string("progress");
    case ExecutionEventType::token: return std::string("token");
    case ExecutionEventType::tool_call: return std::string("tool_call");
    case ExecutionEventType::tool_result: return std::string("tool_result");
    case ExecutionEventType::completed: return std::string("completed");
    case ExecutionEventType::failed: return std::string("failed");
    case ExecutionEventType::cancelled: return std::string("cancelled");
    case ExecutionEventType::timeout: return std::string("timeout");
    case ExecutionEventType::skipped: return std::string("skipped");
    case ExecutionEventType::paused: return std::string("paused");
    case ExecutionEventType::resumed: return std::string("resumed");
    }
    return std::string("unknown");
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
