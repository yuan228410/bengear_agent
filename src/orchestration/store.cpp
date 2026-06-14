#include "ben_gear/orchestration/store.hpp"

namespace ben_gear::orchestration {

void ExecutionStore::start(const ExecutionContext& context, ExecutionKind kind) {
    ExecutionSnapshot snapshot;
    snapshot.execution_id = context.execution_id;
    snapshot.parent_id = context.parent_id;
    snapshot.trace_id = context.trace_id;
    snapshot.kind = kind;
    snapshot.status = ExecutionStatus::running;
    snapshot.updated_at = Clock::now();

    std::lock_guard lock(mutex_);
    active_[snapshot.execution_id] = std::move(snapshot);
}

void ExecutionStore::update(const ExecutionEvent& event) {
    std::lock_guard lock(mutex_);
    auto active_it = active_.find(event.execution_id);
    if (active_it != active_.end()) {
        active_it->second.status = event.status;
        active_it->second.updated_at = event.timestamp;
        if (!event.message.empty()) {
            active_it->second.last_error = event.message;
        }
        return;
    }

    auto completed_it = completed_.find(event.execution_id);
    if (completed_it != completed_.end()) {
        completed_it->second.status = event.status;
        completed_it->second.updated_at = event.timestamp;
        if (!event.message.empty()) {
            completed_it->second.last_error = event.message;
        }
    }
}

void ExecutionStore::complete(const ExecutionResult& result) {
    std::lock_guard lock(mutex_);

    ExecutionSnapshot snapshot;
    auto active_it = active_.find(result.execution_id);
    if (active_it != active_.end()) {
        snapshot = active_it->second;
        active_.erase(result.execution_id);
    } else {
        snapshot.execution_id = result.execution_id;
        snapshot.parent_id = result.parent_id;
        snapshot.kind = result.kind;
    }

    snapshot.status = result.status;
    snapshot.result = result;
    snapshot.last_error = result.error;
    snapshot.updated_at = result.completed_at == TimePoint{} ? Clock::now() : result.completed_at;
    completed_[snapshot.execution_id] = std::move(snapshot);
}

bool ExecutionStore::cancel(const ExecutionId& execution_id) {
    std::lock_guard lock(mutex_);
    auto active_it = active_.find(execution_id);
    if (active_it == active_.end()) {
        return false;
    }
    active_it->second.status = ExecutionStatus::cancelled;
    active_it->second.updated_at = Clock::now();
    return true;
}

std::optional<ExecutionSnapshot> ExecutionStore::get(const ExecutionId& execution_id) const {
    std::lock_guard lock(mutex_);
    auto active_it = active_.find(execution_id);
    if (active_it != active_.end()) {
        return active_it->second;
    }
    auto completed_it = completed_.find(execution_id);
    if (completed_it != completed_.end()) {
        return completed_it->second;
    }
    return std::nullopt;
}

ExecutionStoreSnapshot ExecutionStore::snapshot() const {
    ExecutionStoreSnapshot out;
    std::lock_guard lock(mutex_);

    out.active.reserve(active_.size());
    for (const auto& [_, snapshot] : active_) {
        out.active.push_back(snapshot);
        if (snapshot.status == ExecutionStatus::running) {
            ++out.running_count;
        }
    }

    out.completed.reserve(completed_.size());
    for (const auto& [_, snapshot] : completed_) {
        out.completed.push_back(snapshot);
        switch (snapshot.status) {
        case ExecutionStatus::succeeded:
            ++out.completed_count;
            break;
        case ExecutionStatus::failed:
            ++out.failed_count;
            break;
        case ExecutionStatus::cancelled:
            ++out.cancelled_count;
            break;
        case ExecutionStatus::timeout:
            ++out.timeout_count;
            break;
        default:
            break;
        }
    }

    return out;
}

void ExecutionStore::clear_completed() {
    std::lock_guard lock(mutex_);
    completed_.clear();
}

} // namespace ben_gear::orchestration
