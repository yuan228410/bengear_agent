#pragma once

#include "ben_gear/orchestration/event.hpp"

#include <mutex>
#include <optional>

namespace ben_gear::orchestration {

struct ExecutionSnapshot {
    ExecutionId execution_id;
    ParentExecutionId parent_id;
    TraceId trace_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionStatus status = ExecutionStatus::pending;
    ExecutionResult result;
    container::String last_error;
    TimePoint updated_at = Clock::now();
};

struct ExecutionStoreSnapshot {
    container::Vector<ExecutionSnapshot> active;
    container::Vector<ExecutionSnapshot> completed;
    size_t running_count = 0;
    size_t completed_count = 0;
    size_t failed_count = 0;
    size_t cancelled_count = 0;
    size_t timeout_count = 0;
};

/// 内存执行状态存储。Runtime 只写状态，UI/API 只读快照。
class ExecutionStore {
public:
    void start(const ExecutionContext& context, ExecutionKind kind);
    void update(const ExecutionEvent& event);
    void complete(const ExecutionResult& result);
    bool cancel(const ExecutionId& execution_id);

    std::optional<ExecutionSnapshot> get(const ExecutionId& execution_id) const;
    ExecutionStoreSnapshot snapshot() const;
    void clear_completed();

private:
    using SnapshotMap = container::Map<ExecutionId, ExecutionSnapshot>;

    mutable std::mutex mutex_;
    SnapshotMap active_;
    SnapshotMap completed_;
};

} // namespace ben_gear::orchestration
