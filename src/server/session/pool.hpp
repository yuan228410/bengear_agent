#pragma once

#include <unordered_map>
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

namespace ben_gear::server {

namespace container = base::container;

/// 会话锁管理器
class SessionLockManager {
public:
    bool try_acquire(const std::string& session_id);
    void release(const std::string& session_id);
    bool is_locked(const std::string& session_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, bool> locked_;
};

/// 会话池条目
struct SessionEntry {
    std::shared_ptr<agent::runtime::Runtime> runtime;
    std::shared_ptr<workspace::Session> session;
    orchestration::TodoManager todo_manager;
    std::string username;
    std::chrono::steady_clock::time_point last_active;
    mutable std::mutex state_mutex;
    mutable std::mutex run_mutex;
    net::CancellationToken active_cancel;
    std::atomic<bool> active_run{false};
    std::atomic<bool> pending_remove{false};
};

/// 会话池 — 管理多用户多会话
class SessionPool {
public:
    explicit SessionPool(int max_size = 50);

    std::shared_ptr<SessionEntry> get_or_create(
        const std::string& session_id,
        const std::string& username,
        const std::string& workspace,
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    std::shared_ptr<SessionEntry> get(const std::string& session_id,
                                      const std::string& username,
                                      const std::string& workspace);
    void remove(const std::string& session_id,
                const std::string& username,
                const std::string& workspace);
    void cleanup_idle(int timeout_seconds);
    size_t active_count() const;
    void set_history_db(std::shared_ptr<workspace::HistoryDB> db) { history_db_ = std::move(db); }
    bool cancel(const std::string& session_id,
                const std::string& username,
                const std::string& workspace);

private:
    void touch_lru_unlocked(const std::string& key);
    void erase_lru_unlocked(const std::string& key);

    int max_size_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<SessionEntry>> entries_;
    std::list<std::string> lru_order_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_iter_;
    SessionLockManager lock_manager_;
    std::shared_ptr<workspace::HistoryDB> history_db_;
};

} // namespace ben_gear::server
