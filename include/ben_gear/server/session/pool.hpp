#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/agent/agent.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>

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
    std::shared_ptr<agent::Agent> agent;
    std::shared_ptr<workspace::Session> session;
    std::string username;
    std::chrono::steady_clock::time_point last_active;
};

/// 会话池
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

private:
    int max_size_;
    mutable std::shared_mutex mutex_;
    container::Map<container::String, std::shared_ptr<SessionEntry>> entries_;
    container::Vector<container::String> lru_order_;
    SessionLockManager lock_manager_;
};

} // namespace ben_gear::server
