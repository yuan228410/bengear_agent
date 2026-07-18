#pragma once

#include "workspace/history_db.hpp"
#include <sqlite3.h>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <cstring>
#include "base/platform/os.hpp"
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

} // namespace ben_gear::workspace
