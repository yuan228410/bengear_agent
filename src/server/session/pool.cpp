#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <chrono>

namespace ben_gear::server {

namespace {
container::String make_session_key(const container::String& username,
                                   const container::String& workspace,
                                   const container::String& session_id) {
    container::String key;
    key.append(username);
    key.append("/");
    key.append(workspace);
    key.append("/");
    key.append(session_id);
    return key;
}
}

bool SessionLockManager::try_acquire(const container::String& sid) {
    std::lock_guard lock(mutex_);
    if (locked_.count(sid) && locked_.at(sid)) return false;
    locked_[sid] = true;
    return true;
}

void SessionLockManager::release(const container::String& sid) {
    std::lock_guard lock(mutex_);
    locked_.erase(sid);
}

bool SessionLockManager::is_locked(const container::String& sid) const {
    std::lock_guard lock(mutex_);
    return locked_.count(sid) && locked_.at(sid);
}

SessionPool::SessionPool(int max_size) : max_size_(max_size) {}

std::shared_ptr<SessionEntry> SessionPool::get_or_create(
    const container::String& session_id,
    const container::String& username,
    const container::String& /*workspace*/,
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    auto key = make_session_key(username, ws_ctx.workspace_name, session_id);
    {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            it->second->last_active = std::chrono::steady_clock::now();
            log::info_fmt("SessionPool: reuse {} user={} workspace={} project_path={}",
                          session_id.c_str(), username.c_str(),
                          ws_ctx.workspace_name.c_str(), ws_ctx.project_path.c_str());
            return it->second;
        }
    }
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second->last_active = std::chrono::steady_clock::now();
        log::info_fmt("SessionPool: reuse {} user={} workspace={} project_path={}",
                      session_id.c_str(), username.c_str(),
                      ws_ctx.workspace_name.c_str(), ws_ctx.project_path.c_str());
        return it->second;
    }

    while (static_cast<int>(entries_.size()) >= max_size_ && !lru_order_.empty()) {
        auto oldest = lru_order_.front();
        lru_order_.erase(lru_order_.begin());
        entries_.erase(oldest);
        log::info_fmt("SessionPool: LRU evicted {}", oldest.c_str());
    }

    auto entry = std::make_shared<SessionEntry>();
    entry->agent = std::make_shared<agent::Agent>(std::move(settings), ws_ctx);
    auto res = entry->agent->resources();
    entry->session = std::shared_ptr<workspace::Session>(
        new workspace::Session(
            workspace::SessionConfig{session_id, res->settings().context_length, res->settings().context_prune, agent::SessionType::main, container::String()},
            res->make_session_deps(),
            res->tools_mut()));
    entry->session->restore_from_db(entry->agent->history_db());

    entry->username = username.c_str();
    entry->last_active = std::chrono::steady_clock::now();
    entries_[key] = entry;
    lru_order_.push_back(key);
    log::info_fmt("SessionPool: created {} for user {} workspace={} project_path={}",
                  session_id.c_str(), username.c_str(),
                  ws_ctx.workspace_name.c_str(), ws_ctx.project_path.c_str());
    return entry;
}

std::shared_ptr<SessionEntry> SessionPool::get(const container::String& session_id,
                                                const container::String& username,
                                                const container::String& workspace) {
    auto key = make_session_key(username, workspace, session_id);
    std::shared_lock lock(mutex_);
    auto it = entries_.find(key);
    return (it != entries_.end()) ? it->second : nullptr;
}

void SessionPool::remove(const container::String& session_id,
                         const container::String& username,
                         const container::String& workspace) {
    auto key = make_session_key(username, workspace, session_id);
    std::unique_lock lock(mutex_);
    entries_.erase(key);
    for (auto it = lru_order_.begin(); it != lru_order_.end(); ++it) {
        if (*it == key) { lru_order_.erase(it); break; }
    }
}

void SessionPool::cleanup_idle(int timeout_seconds) {
    auto now = std::chrono::steady_clock::now();
    std::unique_lock lock(mutex_);
    container::Vector<container::String> to_remove;
    for (auto& [k, v] : entries_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - v->last_active).count();
        if (elapsed > timeout_seconds) { log::info_fmt("SessionPool: cleanup idle {}", k.c_str()); to_remove.push_back(k); }
    }
    for (auto& k : to_remove) { entries_.erase(k); }
}

size_t SessionPool::active_count() const { std::shared_lock lock(mutex_); return entries_.size(); }

} // namespace ben_gear::server
