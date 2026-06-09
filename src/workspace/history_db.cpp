#include "ben_gear/workspace/history_db.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <shared_mutex>
#include <chrono>
#include <cstring>

namespace ben_gear::workspace {

struct HistoryDB::Impl {
    sqlite3* db = nullptr;
    mutable std::shared_mutex rw_mutex;   // 保护 DB 读写协调
    std::mutex queue_mutex;               // 保护写入队列
    std::condition_variable queue_cv;     // 通知刷盘线程
    std::mutex flush_mutex;               // flush() 独立锁，不阻塞 append
    std::condition_variable flush_cv;     // flush() 等待用
    std::deque<WriteItem> write_queue;
    std::thread flush_thread;
    std::atomic<bool> running{true};
    std::atomic<int64_t> pending_count{0};
    std::filesystem::path db_path;

    // 全局序列号，保证消息严格有序
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
                session_id TEXT NOT NULL,
                workspace TEXT NOT NULL,
                name TEXT DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                UNIQUE(workspace, session_id)
            );
            CREATE INDEX IF NOT EXISTS idx_sessions_ws ON sessions(workspace);
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
                workspace TEXT NOT NULL,
                session_id TEXT NOT NULL,
                seq INTEGER NOT NULL,
                ts INTEGER NOT NULL,
                role TEXT NOT NULL,
                content TEXT,
                tool_call_id TEXT DEFAULT '',
                tool_name TEXT DEFAULT ''
            );
            CREATE INDEX IF NOT EXISTS idx_session ON messages(workspace, session_id);
            CREATE INDEX IF NOT EXISTS idx_session_seq ON messages(workspace, session_id, seq);
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

        // 触发器：仅对非 tool 角色消息同步到 FTS
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
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        return buf;
    }
};

HistoryDB::HistoryDB(const std::filesystem::path& db_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->db_path = db_path;
    std::filesystem::create_directories(db_path.parent_path());

    // SQLITE_OPEN_FULLMUTEX: SQLite 序列化模式，多线程安全
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

void HistoryDB::append(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& role,
                       const container::String& content,
                       const container::String& tool_call_id,
                       const container::String& tool_name) {
    WriteItem item;
    item.workspace = std::string(workspace.data(), workspace.size());
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

void HistoryDB::update_latest(const container::String& workspace,
                               const container::String& session_id,
                               const container::String& role,
                               const container::String& content) {
    // 同步执行：流式更新需要实时生效
    // SQLite FULLMUTEX 模式保证与 flush_batch 的写操作互斥
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        UPDATE messages SET content=?
        WHERE id = (
            SELECT id FROM messages
            WHERE workspace=? AND session_id=? AND role=?
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
    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    std::string r(role.data(), role.size());
    sqlite3_bind_text(stmt, 1, c.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, r.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB update_latest step failed: {}", sqlite3_errmsg(impl_->db));
    }
    sqlite3_finalize(stmt);
}

void HistoryDB::flush() {
    // 使用独立的 flush_mutex，不阻塞 append() 的 queue_mutex
    std::unique_lock<std::mutex> lock(impl_->flush_mutex);
    impl_->queue_cv.notify_one();  // 唤醒刷盘线程
    impl_->flush_cv.wait(lock, [this] {
        return impl_->pending_count.load(std::memory_order_acquire) == 0;
    });
}

void HistoryDB::flush_loop() {
    log::debug_fmt("HistoryDB flush thread started");
    while (impl_->running.load(std::memory_order_acquire)) {
        std::deque<WriteItem> batch;
        {
            std::unique_lock<std::mutex> lock(impl_->queue_mutex);
            impl_->queue_cv.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return !impl_->write_queue.empty() ||
                               !impl_->running.load(std::memory_order_acquire); });
            if (!impl_->write_queue.empty()) {
                batch.swap(impl_->write_queue);
            }
        }
        if (!batch.empty()) {
            flush_batch(batch);
        }
    }
    // 优雅关闭
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (!impl_->write_queue.empty()) {
            auto remaining = std::move(impl_->write_queue);
            flush_batch(remaining);
        }
    }
    log::debug_fmt("HistoryDB flush thread stopped");
}

void HistoryDB::flush_batch(std::deque<WriteItem>& batch) {
    std::unique_lock<std::shared_mutex> rw_lock(impl_->rw_mutex);

    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* msg_sql =
        "INSERT INTO messages(workspace, session_id, seq, ts, role, content, tool_call_id, tool_name) "
        "VALUES(?,?,?,?,?,?,?,?)";
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
    for (auto& item : batch) {
        sqlite3_bind_text(msg_stmt, 1, item.workspace.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 2, item.session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(msg_stmt, 3, item.seq);
        sqlite3_bind_int64(msg_stmt, 4, item.ts);
        sqlite3_bind_text(msg_stmt, 5, item.role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 6, item.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 7, item.tool_call_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(msg_stmt, 8, item.tool_name.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(msg_stmt);
        if (rc != SQLITE_DONE) {
            log::error_fmt("HistoryDB flush step failed: {}", sqlite3_errmsg(impl_->db));
        }
        sqlite3_reset(msg_stmt);

        upsert_session_meta(item.workspace, item.session_id, item.ts);
    }
    sqlite3_finalize(msg_stmt);

    sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr);

    rw_lock.unlock();  // 先释放写锁，减少持锁时间

    // 更新计数并通知 flush() 等待者
    impl_->pending_count.fetch_sub(static_cast<int64_t>(batch_size),
                                    std::memory_order_release);
    impl_->flush_cv.notify_all();
}

void HistoryDB::upsert_session_meta(const std::string& workspace,
                                     const std::string& session_id,
                                     int64_t ts) {
    const char* sql = R"(
        INSERT INTO sessions(session_id, workspace, name, created_at, updated_at)
        VALUES(?, ?, '', ?, ?)
        ON CONFLICT(workspace, session_id) DO UPDATE SET updated_at=?
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB upsert_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, workspace.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, ts);
    sqlite3_bind_int64(stmt, 4, ts);
    sqlite3_bind_int64(stmt, 5, ts);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB upsert_session step failed: {}", sqlite3_errmsg(impl_->db));
    }
    sqlite3_finalize(stmt);
}

container::Vector<Json> HistoryDB::load_session(
    const container::String& workspace,
    const container::String& session_id,
    int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sql =
        "SELECT id, seq, ts, role, content, tool_call_id, tool_name "
        "FROM messages WHERE workspace=? AND session_id=? ORDER BY seq ASC";
    if (limit > 0) {
        sql += " LIMIT " + std::to_string(limit);
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB load_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);

    container::Vector<Json> results;
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

container::Vector<Json> HistoryDB::list_sessions(
    const container::String& workspace) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = R"(
        SELECT session_id, name, created_at, updated_at,
               (SELECT COUNT(*) FROM messages m
                WHERE m.workspace=s.workspace AND m.session_id=s.session_id) as msg_count
        FROM sessions s
        WHERE workspace=?
        ORDER BY updated_at DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB list_sessions prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string ws(workspace.data(), workspace.size());
    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);

    container::Vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json s;
        auto* sid = sqlite3_column_text(stmt, 0);
        auto* name = sqlite3_column_text(stmt, 1);
        s["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        s["name"] = name ? reinterpret_cast<const char*>(name) : "";
        s["created_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        s["updated_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 3));
        s["msg_count"] = sqlite3_column_int(stmt, 4);
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

bool HistoryDB::rename_session(const container::String& workspace,
                                const container::String& session_id,
                                const container::String& name) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = "UPDATE sessions SET name=? WHERE workspace=? AND session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB rename_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }

    std::string n(name.data(), name.size());
    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, n.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool HistoryDB::delete_session(const container::String& workspace,
                                const container::String& session_id) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* msg_sql = "DELETE FROM messages WHERE workspace=? AND session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, msg_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_session msg prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }
    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;

    const char* sess_sql = "DELETE FROM sessions WHERE workspace=? AND session_id=?";
    rc = sqlite3_prepare_v2(impl_->db, sess_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_session sess prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

container::Vector<Json> HistoryDB::search(
    const container::String& keyword,
    const container::String& workspace,
    int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string kw(keyword.data(), keyword.size());

    if (impl_->has_fts()) {
        std::string sql = R"(
            SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, m.workspace, m.session_id
            FROM messages_fts f
            JOIN messages m ON m.id = f.rowid
            WHERE messages_fts MATCH ?
        )";
        if (!workspace.empty()) {
            sql += " AND m.workspace=?";
        }
        sql += " ORDER BY m.seq DESC LIMIT " + std::to_string(limit);

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, kw.c_str(), -1, SQLITE_TRANSIENT);
            if (!workspace.empty()) {
                std::string ws(workspace.data(), workspace.size());
                sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
            }

            container::Vector<Json> results;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Json msg;
                msg["id"] = sqlite3_column_int64(stmt, 0);
                msg["seq"] = sqlite3_column_int64(stmt, 1);
                msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
                auto* role = sqlite3_column_text(stmt, 3);
                auto* content = sqlite3_column_text(stmt, 4);
                auto* tc_name = sqlite3_column_text(stmt, 5);
                auto* ws = sqlite3_column_text(stmt, 6);
                auto* sid = sqlite3_column_text(stmt, 7);
                msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
                msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
                msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
                msg["workspace"] = ws ? reinterpret_cast<const char*>(ws) : "";
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
        "SELECT id, seq, ts, role, content, tool_name, workspace, session_id "
        "FROM messages WHERE content LIKE ?";
    if (!workspace.empty()) {
        sql += " AND workspace=?";
    }
    sql += " ORDER BY seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string pattern = "%" + kw + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace.empty()) {
        std::string ws(workspace.data(), workspace.size());
        sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    }

    container::Vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_name = sqlite3_column_text(stmt, 5);
        auto* ws = sqlite3_column_text(stmt, 6);
        auto* sid = sqlite3_column_text(stmt, 7);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        msg["workspace"] = ws ? reinterpret_cast<const char*>(ws) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

container::Vector<Json> HistoryDB::search_by_time(
    const container::String& workspace,
    int64_t start_ts,
    int64_t end_ts,
    int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string sql =
        "SELECT id, seq, ts, role, content, tool_name, session_id "
        "FROM messages WHERE 1=1";
    if (!workspace.empty()) {
        sql += " AND workspace=?";
    }
    if (start_ts > 0) {
        sql += " AND ts>=?";
    }
    if (end_ts > 0) {
        sql += " AND ts<=?";
    }
    sql += " ORDER BY seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search_by_time prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    int idx = 1;
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

    container::Vector<Json> results;
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

container::Vector<Json> HistoryDB::search_keyword_time(
    const container::String& keyword,
    const container::String& workspace,
    int64_t start_ts,
    int64_t end_ts,
    int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    std::string kw(keyword.data(), keyword.size());

    if (impl_->has_fts()) {
        std::string sql = R"(
            SELECT m.id, m.seq, m.ts, m.role, m.content, m.tool_name, m.workspace, m.session_id
            FROM messages_fts f
            JOIN messages m ON m.id = f.rowid
            WHERE messages_fts MATCH ?
        )";
        if (!workspace.empty()) {
            sql += " AND m.workspace=?";
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

            container::Vector<Json> results;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Json msg;
                msg["id"] = sqlite3_column_int64(stmt, 0);
                msg["seq"] = sqlite3_column_int64(stmt, 1);
                msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
                auto* role = sqlite3_column_text(stmt, 3);
                auto* content = sqlite3_column_text(stmt, 4);
                auto* tc_name = sqlite3_column_text(stmt, 5);
                auto* ws = sqlite3_column_text(stmt, 6);
                auto* sid = sqlite3_column_text(stmt, 7);
                msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
                msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
                msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
                msg["workspace"] = ws ? reinterpret_cast<const char*>(ws) : "";
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
        "SELECT id, seq, ts, role, content, tool_name, workspace, session_id "
        "FROM messages WHERE content LIKE ?";
    if (!workspace.empty()) {
        sql += " AND workspace=?";
    }
    if (start_ts > 0) {
        sql += " AND ts>=?";
    }
    if (end_ts > 0) {
        sql += " AND ts<=?";
    }
    sql += " ORDER BY seq DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search_keyword_time prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string pattern = "%" + kw + "%";
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
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

    container::Vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        msg["seq"] = sqlite3_column_int64(stmt, 1);
        msg["ts"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        auto* role = sqlite3_column_text(stmt, 3);
        auto* content = sqlite3_column_text(stmt, 4);
        auto* tc_name = sqlite3_column_text(stmt, 5);
        auto* ws = sqlite3_column_text(stmt, 6);
        auto* sid = sqlite3_column_text(stmt, 7);
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        msg["tool_name"] = tc_name ? reinterpret_cast<const char*>(tc_name) : "";
        msg["workspace"] = ws ? reinterpret_cast<const char*>(ws) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

}  // namespace ben_gear::workspace
