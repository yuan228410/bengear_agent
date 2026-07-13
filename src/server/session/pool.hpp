#pragma once

#include "base/container/string.hpp"
#include "base/container/map.hpp"
#include "workspace/types.hpp"
#include "workspace/session.hpp"
#include "agent/runtime/runtime.hpp"
#include "base/net/cancel.hpp"
#include "orchestration/plan.hpp"
#include "orchestration/todo.hpp"

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace ben_gear::server {

namespace container = base::container;

/// 会话锁管理器
class SessionLockManager {
public:
    bool try_acquire(const container::String& session_id);
    void release(const container::String& session_id);
    bool is_locked(const container::String& session_id) const;

private:
    mutable std::mutex mutex_;
    container::Map<container::String, bool> locked_;
};

/// 会话池条目
struct SessionEntry {
    std::shared_ptr<agent::runtime::Runtime> runtime;
    std::shared_ptr<workspace::Session> session;
    orchestration::PlanManager plan_manager;
    orchestration::TodoManager todo_manager;
    std::string username;
    std::chrono::steady_clock::time_point last_active;
    mutable std::mutex state_mutex;
    mutable std::mutex run_mutex;
    net::CancellationToken active_cancel;
    bool active_run = false;
    std::atomic<bool> pending_remove{false};
};

/// 会话池 — 管理多用户多会话
class SessionPool {
public:
    explicit SessionPool(int max_size = 50);

    std::shared_ptr<SessionEntry> get_or_create(
        const container::String& session_id,
        const container::String& username,
        const container::String& workspace,
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    std::shared_ptr<SessionEntry> get(const container::String& session_id,
                                      const container::String& username,
                                      const container::String& workspace);
    void remove(const container::String& session_id,
                const container::String& username,
                const container::String& workspace);
    void cleanup_idle(int timeout_seconds);
    size_t active_count() const;
    SessionLockManager& lock_manager() { return lock_manager_; }
    bool cancel(const container::String& session_id,
                const container::String& username,
                const container::String& workspace);

private:
    void touch_lru_unlocked(const container::String& key);
    void erase_lru_unlocked(const container::String& key);

    int max_size_;
    mutable std::shared_mutex mutex_;
    container::Map<container::String, std::shared_ptr<SessionEntry>> entries_;
    std::list<container::String> lru_order_;
    std::unordered_map<container::String, std::list<container::String>::iterator> lru_iter_;
    SessionLockManager lock_manager_;
};

} // namespace ben_gear::server
