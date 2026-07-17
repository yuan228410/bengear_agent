#include "workspace/history_db.hpp"

#include <sqlite3.h>

#include "base/platform/os.hpp"

#include <filesystem>
#include <shared_mutex>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include "base/log/logger.hpp"

namespace ben_gear::workspace {

struct HistoryDB::Impl {
    sqlite3* db = nullptr;
    mutable std::shared_mutex rw_mutex;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::mutex flush_mutex;
    std::condition_variable flush_cv;
    std::deque<WriteItem> write_queue;
    std::deque<StateWriteItem> state_write_queue;
    std::thread flush_thread;
    std::atomic<bool> running{true};
    std::atomic<int64_t> pending_count{0};
    std::filesystem::path db_path;

    std::atomic<int64_t> global_seq{0};

    void init_seq() {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db,
            "SELECT COALESCE(MAX(seq),0) FROM messages", -1, &stmt, nullptr);
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            global_seq.store(sqlite3_column_int64(stmt, 0), std::memory_order_relaxed);
        }
        sqlite3_finalize(stmt);
        log::info_fmt("HistoryDB seq init: max_seq={}", global_seq.load());
    }

    int64_t next_seq() {
        return global_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    static int64_t now_ts() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool ensure_schema() {
        const char* sessions_sql = R"(
            CREATE TABLE IF NOT EXISTS sessions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL UNIQUE,
                user TEXT NOT NULL DEFAULT 'default',
                workspace TEXT NOT NULL,
                name TEXT DEFAULT '',
                session_type TEXT DEFAULT 'main',
                parent_id TEXT DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user);
            CREATE INDEX IF NOT EXISTS idx_sessions_ws ON sessions(workspace);
            CREATE INDEX IF NOT EXISTS idx_sessions_user_ws ON sessions(user, workspace);
            CREATE INDEX IF NOT EXISTS idx_sessions_type ON sessions(user, workspace, session_type);
            CREATE INDEX IF NOT EXISTS idx_sessions_parent ON sessions(parent_id);
        )";
        char* err = nullptr;
        int rc = sqlite3_exec(db, sessions_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB sessions schema error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }

        const char* msg_sql = R"(
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                seq INTEGER NOT NULL,
                ts INTEGER NOT NULL,
                role TEXT NOT NULL,
                content TEXT,
                tool_call_id TEXT DEFAULT '',
                tool_name TEXT DEFAULT ''
            );
            CREATE INDEX IF NOT EXISTS idx_msg_session ON messages(session_id);
            CREATE INDEX IF NOT EXISTS idx_msg_session_seq ON messages(session_id, seq);
            CREATE INDEX IF NOT EXISTS idx_ts ON messages(ts);
            CREATE INDEX IF NOT EXISTS idx_role ON messages(role);
        )";
        err = nullptr;
        rc = sqlite3_exec(db, msg_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB messages schema error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }

        const char* state_sql = R"(
            CREATE TABLE IF NOT EXISTS session_states (
                session_id TEXT NOT NULL,
                state_type TEXT NOT NULL,
                state_json TEXT NOT NULL,
                updated_at INTEGER NOT NULL,
                PRIMARY KEY(session_id, state_type)
            );
        )";
        err = nullptr;
        rc = sqlite3_exec(db, state_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB session_states schema error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }

        const char* fts_sql = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts
            USING fts5(content, content='messages', content_rowid='id',
                       tokenize='unicode61 remove_diacritics 0');
        )";
        err = nullptr;
        rc = sqlite3_exec(db, fts_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB FTS5 schema error: {}", err ? err : "unknown");
            sqlite3_free(err);
        }

        const char* trigger_sql = R"(
            CREATE TRIGGER IF NOT EXISTS fts_insert AFTER INSERT ON messages
            WHEN new.role != 'tool' BEGIN
                INSERT INTO messages_fts(rowid, content) VALUES (new.id, new.content);
            END;
            CREATE TRIGGER IF NOT EXISTS fts_delete AFTER DELETE ON messages
            WHEN old.role != 'tool' BEGIN
                INSERT INTO messages_fts(messages_fts, rowid, content)
                    VALUES ('delete', old.id, old.content);
            END;
            CREATE TRIGGER IF NOT EXISTS fts_update AFTER UPDATE ON messages
            WHEN new.role != 'tool' BEGIN
                INSERT INTO messages_fts(messages_fts, rowid, content)
                    VALUES ('delete', old.id, old.content);
                INSERT INTO messages_fts(rowid, content) VALUES (new.id, new.content);
            END;
        )";
        err = nullptr;
        rc = sqlite3_exec(db, trigger_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB FTS trigger error: {}", err ? err : "unknown");
            sqlite3_free(err);
        }

        return true;
    }

    bool has_fts() const {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db,
            "SELECT count(*) FROM messages_fts LIMIT 1", -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_ROW || rc == SQLITE_DONE;
    }

    static std::string format_ts(int64_t unix_ts) {
        time_t t = static_cast<time_t>(unix_ts);
        auto tm_buf = ben_gear::base::platform::compat::safe_localtime(t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        return buf;
    }
};

HistoryDB::HistoryDB(const std::filesystem::path& db_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->db_path = db_path;
    std::filesystem::create_directories(db_path.parent_path());

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(db_path.string().c_str(), &impl_->db, flags, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB open failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    impl_->ensure_schema();
    impl_->init_seq();

    impl_->flush_thread = std::thread([this]() { flush_loop(); });
    log::info_fmt("HistoryDB opened: {}", db_path.string());
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
        log::error_fmt("HistoryDB flush prepare failed: {}", sqlite3_errmsg(impl_->db));
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
        log::error_fmt("HistoryDB flush_state_batch prepare failed: {}", sqlite3_errmsg(impl_->db));
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

// ── 会话列表/元数据 ─────────────────────────────────────────────

std::vector<Json> HistoryDB::list_sessions(const std::string& user,
                                            const std::string& workspace,
                                            config::SessionType type_filter) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        SELECT session_id, name, session_type, parent_id, created_at, updated_at,
               (SELECT COUNT(*) FROM messages m WHERE m.session_id=s.session_id) as msg_count
        FROM sessions s
        WHERE user=? AND workspace=? AND session_type=?
        ORDER BY updated_at DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB list_sessions prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    static const char* type_names[] = {"main", "sub_agent", "workflow"};
    auto type_str = type_names[static_cast<size_t>(type_filter)];

    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type_str, -1, SQLITE_TRANSIENT);

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json s;
        auto* sid = sqlite3_column_text(stmt, 0);
        auto* name = sqlite3_column_text(stmt, 1);
        auto* stype = sqlite3_column_text(stmt, 2);
        auto* pid = sqlite3_column_text(stmt, 3);
        s["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        s["name"] = name ? reinterpret_cast<const char*>(name) : "";
        s["session_type"] = stype ? reinterpret_cast<const char*>(stype) : "main";
        s["parent_id"] = pid ? reinterpret_cast<const char*>(pid) : "";
        s["created_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 4));
        s["updated_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 5));
        s["msg_count"] = sqlite3_column_int(stmt, 6);
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::list_all_sessions(const std::string& user,
                                                const std::string& workspace) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        SELECT session_id, name, session_type, parent_id, created_at, updated_at,
               (SELECT COUNT(*) FROM messages m WHERE m.session_id=s.session_id) as msg_count
        FROM sessions s
        WHERE user=? AND workspace=?
        ORDER BY updated_at DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB list_all_sessions prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json s;
        auto* sid = sqlite3_column_text(stmt, 0);
        auto* name = sqlite3_column_text(stmt, 1);
        auto* stype = sqlite3_column_text(stmt, 2);
        auto* pid = sqlite3_column_text(stmt, 3);
        s["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        s["name"] = name ? reinterpret_cast<const char*>(name) : "";
        s["session_type"] = stype ? reinterpret_cast<const char*>(stype) : "main";
        s["parent_id"] = pid ? reinterpret_cast<const char*>(pid) : "";
        s["created_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 4));
        s["updated_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 5));
        s["msg_count"] = sqlite3_column_int(stmt, 6);
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::get_child_sessions(const std::string& user,
                                                 const std::string& workspace,
                                                 const std::string& parent_id) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        SELECT session_id, name, session_type, created_at, updated_at,
               (SELECT COUNT(*) FROM messages m WHERE m.session_id=s.session_id) as msg_count
        FROM sessions s
        WHERE user=? AND workspace=? AND parent_id=?
        ORDER BY created_at ASC
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB get_child_sessions prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());
    std::string pid(parent_id.data(), parent_id.size());
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, pid.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json s;
        auto* sid = sqlite3_column_text(stmt, 0);
        auto* name = sqlite3_column_text(stmt, 1);
        auto* stype = sqlite3_column_text(stmt, 2);
        s["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        s["name"] = name ? reinterpret_cast<const char*>(name) : "";
        s["session_type"] = stype ? reinterpret_cast<const char*>(stype) : "";
        s["created_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 3));
        s["updated_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 4));
        s["msg_count"] = sqlite3_column_int(stmt, 5);
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

void HistoryDB::create_session(const std::string& user,
                               const std::string& workspace,
                               const std::string& session_id,
                               const std::string& name,
                               config::SessionType session_type,
                               const std::string& parent_id) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    static const char* type_names[] = {"main", "sub_agent", "workflow"};
    auto type_str = type_names[static_cast<size_t>(session_type)];

    const char* sql = R"(
        INSERT INTO sessions(session_id, user, workspace, name, session_type, parent_id, created_at, updated_at)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(session_id) DO UPDATE SET
            name=excluded.name, session_type=excluded.session_type,
            parent_id=excluded.parent_id, updated_at=excluded.updated_at
    )";

    auto ts = Impl::now_ts();

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB create_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    std::string nm(name.data(), name.size());
    std::string pid(parent_id.data(), parent_id.size());

    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, nm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, type_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, pid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, ts);
    sqlite3_bind_int64(stmt, 8, ts);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB create_session step failed: {}", sqlite3_errmsg(impl_->db));
    }
    sqlite3_finalize(stmt);
}

bool HistoryDB::rename_session(const std::string& session_id, const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = "UPDATE sessions SET name=? WHERE session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB rename_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }

    std::string n(name.data(), name.size());
    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, n.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ── 会话删除 ────────────────────────────────────────────────────

bool HistoryDB::delete_session(const std::string& session_id) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sid(session_id.data(), session_id.size());

    const char* msg_sql = "DELETE FROM messages WHERE session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, msg_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_session msg prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;

    const char* state_sql = "DELETE FROM session_states WHERE session_id=?";
    rc = sqlite3_prepare_v2(impl_->db, state_sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* sess_sql = "DELETE FROM sessions WHERE session_id=?";
    rc = sqlite3_prepare_v2(impl_->db, sess_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_session sess prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int HistoryDB::delete_all_sessions(const std::string& user,
                                    const std::string& workspace) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());

    sqlite3_stmt* stmt = nullptr;

    // 收集要删除的 session_id
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT session_id FROM sessions WHERE user=? AND workspace=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    if (ids.empty()) return 0;

    int count = static_cast<int>(ids.size());

    // 删除消息
    for (const auto& sid : ids) {
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM messages WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // 删除状态
    for (const auto& sid : ids) {
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM session_states WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // 删除会话元数据
    rc = sqlite3_prepare_v2(impl_->db,
        "DELETE FROM sessions WHERE user=? AND workspace=?", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    log::info_fmt("HistoryDB delete_all_sessions: user={} ws={} count={}", u, ws, count);
    return count;
}

int HistoryDB::delete_sessions_before(const std::string& user,
                                       const std::string& workspace,
                                       int64_t before_ts) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT session_id FROM sessions WHERE user=? AND workspace=? AND updated_at<?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, before_ts);

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    if (ids.empty()) return 0;

    int deleted = 0;
    for (const auto& sid : ids) {
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM messages WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM session_states WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM sessions WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) deleted++;
            sqlite3_finalize(stmt);
        }
    }

    log::info_fmt("HistoryDB delete_sessions_before: user={} ws={} before={} deleted={}", u, ws, before_ts, deleted);
    return deleted;
}

int HistoryDB::delete_sessions_after(const std::string& user,
                                      const std::string& workspace,
                                      int64_t after_ts) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT session_id FROM sessions WHERE user=? AND workspace=? AND updated_at>?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, after_ts);

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    if (ids.empty()) return 0;

    int deleted = 0;
    for (const auto& sid : ids) {
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM messages WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM session_states WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        rc = sqlite3_prepare_v2(impl_->db,
            "DELETE FROM sessions WHERE session_id=?", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) deleted++;
            sqlite3_finalize(stmt);
        }
    }

    log::info_fmt("HistoryDB delete_sessions_after: user={} ws={} after={} deleted={}", u, ws, after_ts, deleted);
    return deleted;
}

int HistoryDB::delete_sessions_by_keyword(const std::string& user,
                                           const std::string& workspace,
                                           const std::string& keyword) {
    auto results = search(keyword, user, workspace, 1000);
    if (results.empty()) return 0;

    std::set<std::string> session_ids;
    for (const auto& r : results) {
        if (r.contains("session_id")) {
            session_ids.insert(r["session_id"].get<std::string>());
        }
    }

    int deleted = 0;
    for (const auto& sid : session_ids) {
        if (delete_session(sid)) {
            deleted++;
        }
    }

    log::info_fmt("HistoryDB delete_sessions_by_keyword: user={} ws={} kw={} deleted={}",
        std::string(user.data(), user.size()),
        std::string(workspace.data(), workspace.size()),
        std::string(keyword.data(), keyword.size()), deleted);
    return deleted;
}

int HistoryDB::delete_messages_before(const std::string& session_id, int64_t before_ts) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string sid(session_id.data(), session_id.size());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "DELETE FROM messages WHERE session_id=? AND ts<?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_messages_before prepare failed: {}", sqlite3_errmsg(impl_->db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, before_ts);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(impl_->db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB delete_messages_before exec failed: {}", sqlite3_errmsg(impl_->db));
        return 0;
    }

    log::info_fmt("HistoryDB delete_messages_before: sid={} before={} deleted={}", sid, before_ts, deleted);
    return deleted;
}

int HistoryDB::delete_messages_by_keyword(const std::string& session_id,
                                           const std::string& keyword) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string sid(session_id.data(), session_id.size());
    std::string kw(keyword.data(), keyword.size());

    sqlite3_stmt* stmt = nullptr;
    std::string pattern = "%" + kw + "%";
    int rc = sqlite3_prepare_v2(impl_->db,
        "DELETE FROM messages WHERE session_id=? AND content LIKE ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_messages_by_keyword prepare failed: {}", sqlite3_errmsg(impl_->db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(impl_->db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB delete_messages_by_keyword exec failed: {}", sqlite3_errmsg(impl_->db));
        return 0;
    }

    log::info_fmt("HistoryDB delete_messages_by_keyword: sid={} kw={} deleted={}", sid, kw, deleted);
    return deleted;
}

// ── 统计 ────────────────────────────────────────────────────────

int64_t HistoryDB::count_messages(const std::string& user, const std::string& workspace) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT COUNT(*) FROM messages m "
        "JOIN sessions s ON m.session_id=s.session_id "
        "WHERE s.user=? AND s.workspace=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int64_t HistoryDB::count_session_messages(const std::string& session_id) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string sid(session_id.data(), session_id.size());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT COUNT(*) FROM messages WHERE session_id=?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, sid.c_str(), -1, SQLITE_TRANSIENT);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int HistoryDB::cleanup_empty_sessions(const std::string& user,
                                       const std::string& workspace) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);
    std::string u(user.data(), user.size());
    std::string ws(workspace.data(), workspace.size());

    const char* sql = R"(
        DELETE FROM sessions WHERE user=? AND workspace=? AND session_id NOT IN (
            SELECT DISTINCT session_id FROM messages
        )
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB cleanup_empty_sessions prepare failed: {}", sqlite3_errmsg(impl_->db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int cleaned = sqlite3_changes(impl_->db);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE && cleaned > 0) {
        log::info_fmt("HistoryDB cleanup_empty_sessions: user={} ws={} cleaned={}", u, ws, cleaned);
    }
    return cleaned;
}

// ── 搜索 ────────────────────────────────────────────────────────

std::vector<Json> HistoryDB::search(const std::string& keyword,
                                     const std::string& user,
                                     const std::string& workspace,
                                     int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string kw(keyword.data(), keyword.size());
    std::string u(user.data(), user.size());

    if (impl_->has_fts()) {
        std::string sql = R"(
            SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, s.workspace, m.session_id
            FROM messages_fts f
            JOIN messages m ON m.id = f.rowid
            JOIN sessions s ON m.session_id = s.session_id
            WHERE messages_fts MATCH ? AND s.user=?
        )";
        if (!workspace.empty()) {
            sql += " AND s.workspace=?";
        }
        sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, kw.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, u.c_str(), -1, SQLITE_TRANSIENT);
            if (!workspace.empty()) {
                std::string ws(workspace.data(), workspace.size());
                sqlite3_bind_text(stmt, 3, ws.c_str(), -1, SQLITE_TRANSIENT);
            }

            std::vector<Json> results;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Json msg;
                msg["id"] = sqlite3_column_int64(stmt, 0);
                msg["seq"] = sqlite3_column_int64(stmt, 1);
                msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
                auto* role = sqlite3_column_text(stmt, 3);
                auto* content = sqlite3_column_text(stmt, 4);
                auto* tc_name = sqlite3_column_text(stmt, 5);
                auto* ws_col = sqlite3_column_text(stmt, 6);
                auto* sid = sqlite3_column_text(stmt, 7);
                msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
                msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
                msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
                msg["workspace"] = ws_col ? reinterpret_cast<const char*>(ws_col) : "";
                msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
                results.push_back(msg);
            }
            sqlite3_finalize(stmt);
            return results;
        }
        sqlite3_finalize(stmt);
        log::warn_fmt("HistoryDB FTS search failed, fallback to LIKE");
    }

    // LIKE 降级
    std::string sql =
        "SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, s.workspace, m.session_id "
        "FROM messages m "
        "JOIN sessions s ON m.session_id = s.session_id "
        "WHERE m.content LIKE ? AND s.user=?";
    if (!workspace.empty()) {
        sql += " AND s.workspace=?";
    }
    sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string pattern = "%" + kw + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace.empty()) {
        std::string ws(workspace.data(), workspace.size());
        sqlite3_bind_text(stmt, 3, ws.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_name = sqlite3_column_text(stmt, 5);
        auto* ws_col = sqlite3_column_text(stmt, 6);
        auto* sid = sqlite3_column_text(stmt, 7);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        msg["workspace"] = ws_col ? reinterpret_cast<const char*>(ws_col) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::search_by_time(const std::string& user,
                                             const std::string& workspace,
                                             int64_t start_ts,
                                             int64_t end_ts,
                                             int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sql =
        "SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, m.session_id "
        "FROM messages m "
        "JOIN sessions s ON m.session_id = s.session_id "
        "WHERE s.user=?";
    if (!workspace.empty()) {
        sql += " AND s.workspace=?";
    }
    if (start_ts > 0) {
        sql += " AND m.ts>=?";
    }
    if (end_ts > 0) {
        sql += " AND m.ts<=?";
    }
    sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search_by_time prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string u(user.data(), user.size());
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, u.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace.empty()) {
        std::string ws(workspace.data(), workspace.size());
        sqlite3_bind_text(stmt, idx++, ws.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (start_ts > 0) {
        sqlite3_bind_int64(stmt, idx++, start_ts);
    }
    if (end_ts > 0) {
        sqlite3_bind_int64(stmt, idx++, end_ts);
    }

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_name = sqlite3_column_text(stmt, 5);
        auto* sid = sqlite3_column_text(stmt, 6);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::search_keyword_time(const std::string& keyword,
                                                  const std::string& user,
                                                  const std::string& workspace,
                                                  int64_t start_ts,
                                                  int64_t end_ts,
                                                  int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string kw(keyword.data(), keyword.size());
    std::string u(user.data(), user.size());

    if (impl_->has_fts()) {
        std::string sql = R"(
            SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, s.workspace, m.session_id
            FROM messages_fts f
            JOIN messages m ON m.id = f.rowid
            JOIN sessions s ON m.session_id = s.session_id
            WHERE messages_fts MATCH ? AND s.user=?
        )";
        if (!workspace.empty()) {
            sql += " AND s.workspace=?";
        }
        if (start_ts > 0) {
            sql += " AND m.ts>=?";
        }
        if (end_ts > 0) {
            sql += " AND m.ts<=?";
        }
        sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            int idx = 1;
            sqlite3_bind_text(stmt, idx++, kw.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, idx++, u.c_str(), -1, SQLITE_TRANSIENT);
            if (!workspace.empty()) {
                std::string ws(workspace.data(), workspace.size());
                sqlite3_bind_text(stmt, idx++, ws.c_str(), -1, SQLITE_TRANSIENT);
            }
            if (start_ts > 0) {
                sqlite3_bind_int64(stmt, idx++, start_ts);
            }
            if (end_ts > 0) {
                sqlite3_bind_int64(stmt, idx++, end_ts);
            }

            std::vector<Json> results;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Json msg;
                msg["id"] = sqlite3_column_int64(stmt, 0);
                msg["seq"] = sqlite3_column_int64(stmt, 1);
                msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
                auto* role = sqlite3_column_text(stmt, 3);
                auto* content = sqlite3_column_text(stmt, 4);
                auto* tc_name = sqlite3_column_text(stmt, 5);
                auto* ws_col = sqlite3_column_text(stmt, 6);
                auto* sid = sqlite3_column_text(stmt, 7);
                msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
                msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
                msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
                msg["workspace"] = ws_col ? reinterpret_cast<const char*>(ws_col) : "";
                msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
                results.push_back(msg);
            }
            sqlite3_finalize(stmt);
            return results;
        }
        sqlite3_finalize(stmt);
        log::warn_fmt("HistoryDB FTS keyword+time search failed, fallback to LIKE");
    }

    std::string sql =
        "SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, s.workspace, m.session_id "
        "FROM messages m "
        "JOIN sessions s ON m.session_id = s.session_id "
        "WHERE m.content LIKE ? AND s.user=?";
    if (!workspace.empty()) {
        sql += " AND s.workspace=?";
    }
    if (start_ts > 0) {
        sql += " AND m.ts>=?";
    }
    if (end_ts > 0) {
        sql += " AND m.ts<=?";
    }
    sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search_keyword_time prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string pattern = "%" + kw + "%";
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, u.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace.empty()) {
        std::string ws(workspace.data(), workspace.size());
        sqlite3_bind_text(stmt, idx++, ws.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (start_ts > 0) {
        sqlite3_bind_int64(stmt, idx++, start_ts);
    }
    if (end_ts > 0) {
        sqlite3_bind_int64(stmt, idx++, end_ts);
    }

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_name = sqlite3_column_text(stmt, 5);
        auto* ws_col = sqlite3_column_text(stmt, 6);
        auto* sid = sqlite3_column_text(stmt, 7);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        msg["workspace"] = ws_col ? reinterpret_cast<const char*>(ws_col) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

}  // namespace ben_gear::workspace
