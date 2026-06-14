#pragma once

#include "ben_gear/llm/usage.hpp"
#include "ben_gear/orchestration/types.hpp"

#include <utility>

namespace ben_gear::orchestration {

/// 结构化值：内部优先保持 String，JSON 只在边界生成。
struct ExecutionValue {
    container::String text;
    Metadata fields;

    bool empty() const noexcept {
        return text.empty() && fields.empty();
    }
};

/// 子执行摘要。避免 ExecutionResult 递归持有自身，降低拷贝和模板实例化成本。
struct ExecutionChildSummary {
    ExecutionId execution_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionStatus status = ExecutionStatus::pending;
    ExecutionValue output;
    container::String error;
};

/// 统一执行结果。可表达 sub-agent、workflow task、tool 等输出。
struct ExecutionResult {
    ExecutionId execution_id;
    ParentExecutionId parent_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionStatus status = ExecutionStatus::pending;
    ExecutionValue output;
    container::String error;
    llm::TokenUsage usage;
    llm::RequestLatency latency;
    Metadata metrics;
    container::Vector<ExecutionChildSummary> children;
    TimePoint started_at{};
    TimePoint completed_at{};

    bool success() const noexcept { return status == ExecutionStatus::succeeded; }
    bool terminal() const noexcept { return is_terminal(status); }

    static ExecutionResult ok(ExecutionId id, ExecutionKind kind, ExecutionValue output = {}) {
        ExecutionResult result;
        result.execution_id = std::move(id);
        result.kind = kind;
        result.status = ExecutionStatus::succeeded;
        result.output = std::move(output);
        result.completed_at = Clock::now();
        return result;
    }

    static ExecutionResult failed(ExecutionId id, ExecutionKind kind, container::String error) {
        ExecutionResult result;
        result.execution_id = std::move(id);
        result.kind = kind;
        result.status = ExecutionStatus::failed;
        result.error = std::move(error);
        result.completed_at = Clock::now();
        return result;
    }
};

} // namespace ben_gear::orchestration
