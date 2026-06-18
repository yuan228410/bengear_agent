#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/orchestration/serializer.hpp"

#include <chrono>
#include <stdexcept>

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

void restore_orchestration_state(SessionEntry& entry) {
    auto& db = entry.agent->history_db();
    const auto& workspace = entry.session->workspace_context().workspace_name;
    const auto& session_id = entry.session->session_id();
    std::string error;

    auto plan_json = db.load_session_state(workspace, session_id, container::String("plan"));
    if (!plan_json.empty()) {
        auto parsed = parse_json(std::string_view(plan_json.data(), plan_json.size()), error);
        if (error.empty() && parsed.is_object()) {
            entry.plan_manager.restore(orchestration::plan_draft_from_json(parsed));
        } else {
            log::warn_fmt("SessionPool: failed to restore plan state session={} error={}", session_id.c_str(), error.c_str());
        }
    }

    error.clear();
    auto todo_json = db.load_session_state(workspace, session_id, container::String("todo"));
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

void SessionPool::erase_lru_unlocked(const container::String& key) {
    for (auto it = lru_order_.begin(); it != lru_order_.end(); ++it) {
        if (*it == key) {
            lru_order_.erase(it);
            return;
        }
    }
}

void SessionPool::touch_lru_unlocked(const container::String& key) {
    erase_lru_unlocked(key);
    lru_order_.push_back(key);
}

std::shared_ptr<SessionEntry> SessionPool::get_or_create(
    const container::String& session_id,
    const container::String& username,
    const container::String& /*workspace*/,
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
                lru_order_.erase(lru_it);
                evicted = true;
                break;
            }
            std::lock_guard run_lock(entry_it->second->run_mutex);
            if (entry_it->second->active_run) {
                continue;
            }
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
    entry->agent = std::make_shared<agent::Agent>(std::move(settings), ws_ctx);
    auto res = entry->agent->resources();
    entry->session = std::shared_ptr<workspace::Session>(
        new workspace::Session(
            workspace::SessionConfig{session_id, res->settings().context_length, res->settings().context_prune, agent::SessionType::main, container::String()},
            res->make_session_deps(),
            res->tools_mut()));
    entry->session->restore_from_db(entry->agent->history_db());
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

std::shared_ptr<SessionEntry> SessionPool::get(const container::String& session_id,
                                                const container::String& username,
                                                const container::String& workspace) {
    auto key = make_session_key(username, workspace, session_id);
    std::unique_lock lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    it->second->last_active = std::chrono::steady_clock::now();
    touch_lru_unlocked(key);
    return it->second;
}

void SessionPool::remove(const container::String& session_id,
                         const container::String& username,
                         const container::String& workspace) {
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

bool SessionPool::cancel(const container::String& session_id,
                         const container::String& username,
                         const container::String& workspace) {
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
    container::Vector<container::String> to_remove;
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
