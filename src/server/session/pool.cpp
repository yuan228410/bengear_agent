#include "server/session/pool.hpp"
#include "agent/runtime/runtime_factory.hpp"
#include "base/log/logger.hpp"
#include "base/utils/json.hpp"
#include "orchestration/serializer.hpp"

#include <chrono>
#include <stdexcept>

namespace ben_gear::server {

namespace {
std::string make_session_key(const std::string& username,
                                   const std::string& workspace,
                                   const std::string& session_id) {
    std::string key;
    key.append(username);
    key.append("/");
    key.append(workspace);
    key.append("/");
    key.append(session_id);
    return key;
}

void restore_orchestration_state(SessionEntry& entry) {
    auto& db = entry.runtime->history_db();
    const auto& session_id = entry.session->session_id();
    std::string error;

    auto plan_json = db.load_session_state(session_id, std::string("plan"));
    if (!plan_json.empty()) {
        auto parsed = parse_json(std::string_view(plan_json.data(), plan_json.size()), error);
        if (error.empty() && parsed.is_object()) {
            entry.plan_manager.restore(orchestration::plan_draft_from_json(parsed));
        } else {
            log::warn_fmt("SessionPool: failed to restore plan state session={} error={}", session_id.c_str(), error.c_str());
        }
    }

    error.clear();
    auto todo_json = db.load_session_state(session_id, std::string("todo"));
    if (!todo_json.empty()) {
        auto parsed = parse_json(std::string_view(todo_json.data(), todo_json.size()), error);
        if (error.empty() && parsed.is_object()) {
            entry.todo_manager.restore(orchestration::todo_state_from_json(parsed));
        } else {
            log::warn_fmt("SessionPool: failed to restore todo state session={} error={}", session_id.c_str(), error.c_str());
        }
    }
}
}

bool SessionLockManager::try_acquire(const std::string& sid) {
    std::lock_guard lock(mutex_);
    if (locked_.count(sid) && locked_.at(sid)) return false;
    locked_[sid] = true;
    return true;
}

void SessionLockManager::release(const std::string& sid) {
    std::lock_guard lock(mutex_);
    locked_.erase(sid);
}

bool SessionLockManager::is_locked(const std::string& sid) const {
    std::lock_guard lock(mutex_);
    return locked_.count(sid) && locked_.at(sid);
}

SessionPool::SessionPool(int max_size) : max_size_(max_size) {}

void SessionPool::erase_lru_unlocked(const std::string& key) {
    auto it = lru_iter_.find(key);
    if (it != lru_iter_.end()) {
        lru_order_.erase(it->second);
        lru_iter_.erase(it);
    }
}

void SessionPool::touch_lru_unlocked(const std::string& key) {
    erase_lru_unlocked(key);
    lru_order_.push_back(key);
    lru_iter_[key] = std::prev(lru_order_.end());
}

std::shared_ptr<SessionEntry> SessionPool::get_or_create(
    const std::string& session_id,
    const std::string& username,
    const std::string& /*workspace*/,
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    auto key = make_session_key(username, ws_ctx.workspace_name, session_id);
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        bool erase_pending = false;
        {
            std::lock_guard run_lock(it->second->run_mutex);
            erase_pending = it->second->pending_remove.load(std::memory_order_acquire) && !it->second->active_run;
        }
        if (erase_pending) {
            entries_.erase(key);
            erase_lru_unlocked(key);
        } else {
            it->second->last_active = std::chrono::steady_clock::now();
            touch_lru_unlocked(key);
            log::info_fmt("SessionPool: reuse {} user={} workspace={} project_path={}",
                          session_id.c_str(), username.c_str(),
                          ws_ctx.workspace_name.c_str(), ws_ctx.project_path.c_str());
            return it->second;
        }
    }

    while (static_cast<int>(entries_.size()) >= max_size_ && !lru_order_.empty()) {
        bool evicted = false;
        for (auto lru_it = lru_order_.begin(); lru_it != lru_order_.end(); ++lru_it) {
            auto oldest = *lru_it;
            auto entry_it = entries_.find(oldest);
            if (entry_it == entries_.end()) {
                lru_iter_.erase(oldest);
                lru_order_.erase(lru_it);
                evicted = true;
                break;
            }
            std::lock_guard run_lock(entry_it->second->run_mutex);
            if (entry_it->second->active_run) {
                continue;
            }
            lru_iter_.erase(oldest);
            lru_order_.erase(lru_it);
            entries_.erase(oldest);
            log::info_fmt("SessionPool: LRU evicted {}", oldest.c_str());
            evicted = true;
            break;
        }
        if (!evicted) {
            log::warn_fmt("SessionPool: all sessions active, refusing to evict active session for {}", key.c_str());
            throw std::runtime_error("session pool is full of active sessions");
        }
    }

    auto entry = std::make_shared<SessionEntry>();
    entry->runtime = agent::runtime::RuntimeFactory::create(std::move(settings), ws_ctx);
    if (history_db_) entry->runtime->set_history_db(history_db_);
    auto& rt = *entry->runtime;
    entry->session = std::shared_ptr<workspace::Session>(rt.make_session(session_id).release());
    restore_orchestration_state(*entry);

    entry->username = username.c_str();
    entry->last_active = std::chrono::steady_clock::now();
    entries_[key] = entry;
    touch_lru_unlocked(key);
    log::info_fmt("SessionPool: created {} for user {} workspace={} project_path={}",
                  session_id.c_str(), username.c_str(),
                  ws_ctx.workspace_name.c_str(), ws_ctx.project_path.c_str());
    return entry;
}

std::shared_ptr<SessionEntry> SessionPool::get(const std::string& session_id,
                                                const std::string& username,
                                                const std::string& workspace) {
    auto key = make_session_key(username, workspace, session_id);
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    it->second->last_active = std::chrono::steady_clock::now();
    touch_lru_unlocked(key);
    return it->second;
}

void SessionPool::remove(const std::string& session_id,
                         const std::string& username,
                         const std::string& workspace) {
    auto key = make_session_key(username, workspace, session_id);
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        erase_lru_unlocked(key);
        return;
    }
    {
        std::lock_guard run_lock(it->second->run_mutex);
        if (it->second->active_run) {
            it->second->pending_remove.store(true, std::memory_order_release);
            it->second->active_cancel.cancel();
            log::warn_fmt("SessionPool: defer remove active session {}", key.c_str());
            return;
        }
    }
    entries_.erase(key);
    erase_lru_unlocked(key);
}

bool SessionPool::cancel(const std::string& session_id,
                         const std::string& username,
                         const std::string& workspace) {
    auto entry = get(session_id, username, workspace);
    if (!entry) return false;
    std::lock_guard lock(entry->run_mutex);
    if (!entry->active_run) return false;
    entry->active_cancel.cancel();
    return true;
}

void SessionPool::cleanup_idle(int timeout_seconds) {
    auto now = std::chrono::steady_clock::now();
    std::unique_lock lock(mutex_);
    std::vector<std::string> to_remove;
    for (auto& [k, v] : entries_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - v->last_active).count();
        bool active = false;
        {
            std::lock_guard run_lock(v->run_mutex);
            active = v->active_run;
        }
        if (elapsed > timeout_seconds && !active) { log::info_fmt("SessionPool: cleanup idle {}", k.c_str()); to_remove.push_back(k); }
    }
    for (auto& k : to_remove) {
        entries_.erase(k);
        erase_lru_unlocked(k);
    }
}

size_t SessionPool::active_count() const { std::shared_lock lock(mutex_); return entries_.size(); }

} // namespace ben_gear::server
