#include "workspace/history_db_impl.hpp"
#include <shared_mutex>
#include <mutex>

namespace ben_gear::workspace {

// ── 会话状态 ────────────────────────────────────────────────────

bool HistoryDB::save_session_state(const std::string& session_id,
                                   const std::string& state_type,
                                   const std::string& state_json) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    const char* sql = R"(
        INSERT INTO session_states(session_id, state_type, state_json, updated_at)
        VALUES(?, ?, ?, ?)
        ON CONFLICT(session_id, state_type)
        DO UPDATE SET state_json=excluded.state_json, updated_at=excluded.updated_at
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB save_session_state prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }

    std::string sid(session_id.data(), session_id.size());
    std::string type(state_type.data(), state_type.size());
    std::string json(state_json.data(), state_json.size());
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, Impl::now_ts());
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB save_session_state step failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }
    upsert_session_meta(sid, Impl::now_ts());
    return true;
}

void HistoryDB::save_session_state_async(const std::string& session_id,
                                         const std::string& state_type,
                                         const std::string& state_json) {
    StateWriteItem item;
    item.session_id = std::string(session_id.data(), session_id.size());
    item.state_type = std::string(state_type.data(), state_type.size());
    item.state_json = std::string(state_json.data(), state_json.size());

    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->state_write_queue.push_back(std::move(item));
        impl_->pending_count.fetch_add(1, std::memory_order_relaxed);
    }
    impl_->queue_cv.notify_one();
}

std::string HistoryDB::load_session_state(const std::string& session_id,
                                          const std::string& state_type) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);
    const char* sql = R"(
        SELECT state_json FROM session_states
        WHERE session_id=? AND state_type=?
        LIMIT 1
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB load_session_state prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string sid(session_id.data(), session_id.size());
    std::string type(state_type.data(), state_type.size());
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* text = sqlite3_column_text(stmt, 0);
        if (text) result = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace ben_gear::workspace
