#include "workspace/history_db_impl.hpp"
#include <shared_mutex>
#include <filesystem>
#include <mutex>
#include <thread>

#include <map>

namespace ben_gear::workspace {

HistoryDB::HistoryDB(const std::filesystem::path& db_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->db_path = db_path;
    std::filesystem::create_directories(db_path.parent_path());

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(db_path.string().c_str(), &impl_->db, flags, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB open failed: {} — database unavailable", sqlite3_errmsg(impl_->db));
        impl_->corrupted_ = true;
        return;
    }

    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    impl_->ensure_schema();
    impl_->init_seq();

    impl_->flush_thread = std::thread([this]() { flush_loop(); });
    log::info_fmt("HistoryDB opened: {}", db_path.string());
}

const std::filesystem::path& HistoryDB::db_path() const {
    return impl_->db_path;
}

HistoryDB::~HistoryDB() {
    impl_->running.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->queue_cv.notify_one();
    }
    if (impl_->flush_thread.joinable()) {
        impl_->flush_thread.join();
    }
    if (impl_ && impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

// ── 消息写入 ────────────────────────────────────────────────────

void HistoryDB::append(const std::string& session_id,
                       const std::string& role,
                       const std::string& content,
                       const std::string& tool_call_id,
                       const std::string& tool_name) {
    if (impl_->corrupted_) return;

    WriteItem item;
    item.session_id = std::string(session_id.data(), session_id.size());
    item.seq = impl_->next_seq();
    item.ts = Impl::now_ts();
    item.role = std::string(role.data(), role.size());
    item.content = std::string(content.data(), content.size());
    item.tool_call_id = std::string(tool_call_id.data(), tool_call_id.size());
    item.tool_name = std::string(tool_name.data(), tool_name.size());

    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->write_queue.push_back(std::move(item));
        impl_->pending_count.fetch_add(1, std::memory_order_relaxed);
    }
    impl_->queue_cv.notify_one();
}

void HistoryDB::update_latest(const std::string& session_id,
                               const std::string& role,
                               const std::string& content) {
    if (impl_->corrupted_) return;
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        UPDATE messages SET content=?
        WHERE id = (
            SELECT id FROM messages
            WHERE session_id=? AND role=?
            ORDER BY seq DESC LIMIT 1
        )
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB update_latest prepare failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    std::string c(content.data(), content.size());
    std::string sid(session_id.data(), session_id.size());
    std::string r(role.data(), role.size());
    sqlite3_bind_text(stmt, 1, c.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, r.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB update_latest step failed: {}", sqlite3_errmsg(impl_->db));
    }
    sqlite3_finalize(stmt);
}

// ── 刷盘 ────────────────────────────────────────────────────────

void HistoryDB::flush() {
    std::unique_lock<std::mutex> lock(impl_->flush_mutex);
    impl_->queue_cv.notify_one();
    impl_->flush_cv.wait(lock, [this] {
        return impl_->pending_count.load(std::memory_order_acquire) == 0;
    });
}

void HistoryDB::flush_loop() {
    log::debug_fmt("HistoryDB flush thread started");
    while (impl_->running.load(std::memory_order_acquire)) {
        std::deque<WriteItem> batch;
        std::deque<StateWriteItem> state_batch;
        {
            std::unique_lock<std::mutex> lock(impl_->queue_mutex);
            impl_->queue_cv.wait(lock,
                [this] { return !impl_->write_queue.empty() ||
                               !impl_->state_write_queue.empty() ||
                               !impl_->running.load(std::memory_order_acquire); });
            if (!impl_->write_queue.empty()) {
                batch.swap(impl_->write_queue);
            }
            if (!impl_->state_write_queue.empty()) {
                state_batch.swap(impl_->state_write_queue);
            }
        }
        if (!batch.empty()) {
            flush_batch(batch);
        }
        if (!state_batch.empty()) {
            flush_state_batch(state_batch);
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (!impl_->write_queue.empty()) {
            auto remaining = std::move(impl_->write_queue);
            flush_batch(remaining);
        }
        if (!impl_->state_write_queue.empty()) {
            auto remaining = std::move(impl_->state_write_queue);
            flush_state_batch(remaining);
        }
    }
    log::debug_fmt("HistoryDB flush thread stopped");
}

void HistoryDB::flush_batch(std::deque<WriteItem>& batch) {
    std::unique_lock<std::shared_mutex> rw_lock(impl_->rw_mutex);

    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* msg_sql =
        "INSERT INTO messages(session_id, seq, ts, role, content, tool_call_id, tool_name) "
        "VALUES(?,?,?,?,?,?,?)";
    sqlite3_stmt* msg_stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, msg_sql, -1, &msg_stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB flush prepare failed: {} — marking db unavailable", sqlite3_errmsg(impl_->db));
        impl_->corrupted_ = true;
        sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        impl_->pending_count.fetch_sub(static_cast<int64_t>(batch.size()),
                                        std::memory_order_relaxed);
        impl_->flush_cv.notify_all();
        return;
    }

    auto batch_size = batch.size();
    std::map<std::string, int64_t> session_updates;
    for (auto& item : batch) {
        sqlite3_bind_text(msg_stmt, 1, item.session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(msg_stmt, 2, item.seq);
        sqlite3_bind_int64(msg_stmt, 3, item.ts);
        sqlite3_bind_text(msg_stmt, 4, item.role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 5, item.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 6, item.tool_call_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 7, item.tool_name.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(msg_stmt);
        if (rc != SQLITE_DONE) {
            log::error_fmt("HistoryDB flush step failed: {}", sqlite3_errmsg(impl_->db));
        } else {
            auto it = session_updates.find(item.session_id);
            if (it == session_updates.end() || item.ts > it->second) {
                session_updates[item.session_id] = item.ts;
            }
        }
        sqlite3_reset(msg_stmt);
    }
    sqlite3_finalize(msg_stmt);

    for (const auto& [sid, ts] : session_updates) {
        upsert_session_meta(sid, ts);
    }

    sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr);

    rw_lock.unlock();

    impl_->pending_count.fetch_sub(static_cast<int64_t>(batch_size),
                                    std::memory_order_release);
    impl_->flush_cv.notify_all();
}

void HistoryDB::flush_state_batch(std::deque<StateWriteItem>& batch) {
    std::unique_lock<std::shared_mutex> rw_lock(impl_->rw_mutex);

    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* state_sql = R"(
        INSERT INTO session_states(session_id, state_type, state_json, updated_at)
        VALUES(?, ?, ?, ?)
        ON CONFLICT(session_id, state_type)
        DO UPDATE SET state_json=excluded.state_json, updated_at=excluded.updated_at
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, state_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB flush_state_batch prepare failed: {} — marking db unavailable", sqlite3_errmsg(impl_->db));
        impl_->corrupted_ = true;
        sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        impl_->pending_count.fetch_sub(static_cast<int64_t>(batch.size()),
                                        std::memory_order_relaxed);
        impl_->flush_cv.notify_all();
        return;
    }

    auto batch_size = batch.size();
    std::map<std::string, int64_t> session_updates;
    int64_t now = Impl::now_ts();
    for (auto& item : batch) {
        sqlite3_bind_text(stmt, 1, item.session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, item.state_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, item.state_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, now);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            log::error_fmt("HistoryDB flush_state_batch step failed: {}", sqlite3_errmsg(impl_->db));
        } else {
            auto it = session_updates.find(item.session_id);
            if (it == session_updates.end() || now > it->second) {
                session_updates[item.session_id] = now;
            }
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    for (const auto& [sid, ts] : session_updates) {
        upsert_session_meta(sid, ts);
    }

    sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr);

    rw_lock.unlock();

    impl_->pending_count.fetch_sub(static_cast<int64_t>(batch_size),
                                    std::memory_order_release);
    impl_->flush_cv.notify_all();
}

void HistoryDB::upsert_session_meta(const std::string& session_id, int64_t ts) {
    const char* sql = "UPDATE sessions SET updated_at=? WHERE session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB upsert_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB upsert_session step failed: {}", sqlite3_errmsg(impl_->db));
    }
    sqlite3_finalize(stmt);
}

// ── 消息加载 ────────────────────────────────────────────────────

std::vector<Json> HistoryDB::load_session(const std::string& session_id, int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sql;
    if (limit > 0) {
        sql =
            "SELECT id, seq, ts, role, content, tool_call_id, tool_name FROM ("
            "SELECT id, seq, ts, role, content, tool_call_id, tool_name FROM messages "
            "WHERE session_id=? ORDER BY seq DESC LIMIT " + std::to_string(limit) +
            ") ORDER BY seq ASC";
    } else {
        sql =
            "SELECT id, seq, ts, role, content, tool_call_id, tool_name "
            "FROM messages WHERE session_id=? ORDER BY seq ASC";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB load_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_id = sqlite3_column_text(stmt, 5);
        auto* tc_name = sqlite3_column_text(stmt, 6);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_call_id"] = tc_id ? reinterpret_cast<const char*>(tc_id) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::load_session_chat_messages(const std::string& session_id, int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sql;
    if (limit > 0) {
        sql =
            "SELECT id, seq, ts, role, content FROM ("
            "SELECT id, seq, ts, role, content FROM messages "
            "WHERE session_id=? "
            "AND (role='user' OR role='plan_anchor' OR (role='assistant' AND TRIM(content) <> '')) "
            "ORDER BY seq DESC LIMIT ?"
            ") ORDER BY seq ASC";
    } else {
        sql =
            "SELECT id, seq, ts, role, content FROM messages "
            "WHERE session_id=? "
            "AND (role='user' OR role='plan_anchor' OR (role='assistant' AND TRIM(content) <> '')) "
            "ORDER BY seq ASC";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB load_session_chat_messages prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    if (limit > 0) {
        sqlite3_bind_int(stmt, 2, limit);
    }

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace ben_gear::workspace
