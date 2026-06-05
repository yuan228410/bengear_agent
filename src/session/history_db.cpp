#include "ben_gear/session/history_db.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace ben_gear::session {

struct HistoryDB::Impl {
    sqlite3* db = nullptr;
    mutable std::shared_mutex mutex;
    std::filesystem::path db_path;

    bool ensure_schema() {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                workspace TEXT NOT NULL,
                session_id TEXT NOT NULL,
                ts TEXT NOT NULL,
                role TEXT NOT NULL,
                content TEXT,
                metadata TEXT DEFAULT ''
            );
            CREATE INDEX IF NOT EXISTS idx_session ON messages(workspace, session_id);
            CREATE INDEX IF NOT EXISTS idx_ts ON messages(ts);
        )";
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            log::error_fmt("HistoryDB schema error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }
        return true;
    }
};

HistoryDB::HistoryDB(const std::filesystem::path& db_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->db_path = db_path;

    std::filesystem::create_directories(db_path.parent_path());

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    int rc = sqlite3_open_v2(db_path.string().c_str(), &impl_->db, flags, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB open failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    // WAL 模式：支持并发读
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    impl_->ensure_schema();
    log::info_fmt("HistoryDB opened: {}", db_path.string());
}

HistoryDB::~HistoryDB() {
    if (impl_ && impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

int64_t HistoryDB::append(const container::String& workspace,
                           const container::String& session_id,
                           const container::String& role,
                           const container::String& content,
                           const container::String& metadata) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);

    // 获取 ISO 8601 时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    const char* sql = "INSERT INTO messages(workspace, session_id, ts, role, content, metadata) VALUES(?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB append prepare failed: {}", sqlite3_errmsg(impl_->db));
        return -1;
    }

    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
    std::string r(role.data(), role.size());
    std::string c(content.data(), content.size());
    std::string m(metadata.data(), metadata.size());

    sqlite3_bind_text(stmt, 1, ws.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ts, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, r.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, c.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, m.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB append failed: {}", sqlite3_errmsg(impl_->db));
        return -1;
    }

    return sqlite3_last_insert_rowid(impl_->db);
}

container::Vector<Json> HistoryDB::load_session(
    const container::String& workspace,
    const container::String& session_id,
    int limit) {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);

    std::string sql = "SELECT id, ts, role, content, metadata FROM messages WHERE workspace=? AND session_id=? ORDER BY id ASC";
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
        auto* ts_text = sqlite3_column_text(stmt, 1);
        msg["ts"] = ts_text ? reinterpret_cast<const char*>(ts_text) : "";
        auto* role_text = sqlite3_column_text(stmt, 2);
        msg["role"] = role_text ? reinterpret_cast<const char*>(role_text) : "";
        auto* content_text = sqlite3_column_text(stmt, 3);
        msg["content"] = content_text ? reinterpret_cast<const char*>(content_text) : "";
        auto* meta = sqlite3_column_text(stmt, 4);
        msg["metadata"] = meta ? reinterpret_cast<const char*>(meta) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

container::Vector<Json> HistoryDB::list_sessions(
    const container::String& workspace) {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);

    const char* sql = R"(
        SELECT session_id, MIN(ts) as created_at, MAX(ts) as updated_at, COUNT(*) as msg_count
        FROM messages WHERE workspace=?
        GROUP BY session_id ORDER BY updated_at DESC
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
        auto* created = sqlite3_column_text(stmt, 1);
        auto* updated = sqlite3_column_text(stmt, 2);
        s["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        s["created_at"] = created ? reinterpret_cast<const char*>(created) : "";
        s["updated_at"] = updated ? reinterpret_cast<const char*>(updated) : "";
        s["msg_count"] = sqlite3_column_int(stmt, 3);
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

bool HistoryDB::delete_session(const container::String& workspace,
                                const container::String& session_id) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);

    const char* sql = "DELETE FROM messages WHERE workspace=? AND session_id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB delete_session prepare failed: {}", sqlite3_errmsg(impl_->db));
        return false;
    }

    std::string ws(workspace.data(), workspace.size());
    std::string sid(session_id.data(), session_id.size());
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
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);

    std::string sql = "SELECT id, workspace, session_id, ts, role, content FROM messages WHERE content LIKE ?";
    if (!workspace.empty()) {
        sql += " AND workspace=?";
    }
    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB search prepare failed: {}", sqlite3_errmsg(impl_->db));
        return {};
    }

    std::string pattern = "%" + std::string(keyword.data(), keyword.size()) + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace.empty()) {
        std::string ws(workspace.data(), workspace.size());
        sqlite3_bind_text(stmt, 2, ws.c_str(), -1, SQLITE_TRANSIENT);
    }

    container::Vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json msg;
        msg["id"] = sqlite3_column_int64(stmt, 0);
        auto* ws = sqlite3_column_text(stmt, 1);
        auto* sid = sqlite3_column_text(stmt, 2);
        auto* ts = sqlite3_column_text(stmt, 3);
        auto* role = sqlite3_column_text(stmt, 4);
        auto* content = sqlite3_column_text(stmt, 5);
        msg["workspace"] = ws ? reinterpret_cast<const char*>(ws) : "";
        msg["session_id"] = sid ? reinterpret_cast<const char*>(sid) : "";
        msg["ts"] = ts ? reinterpret_cast<const char*>(ts) : "";
        msg["role"] = role ? reinterpret_cast<const char*>(role) : "";
        msg["content"] = content ? reinterpret_cast<const char*>(content) : "";
        results.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return results;
}

}  // namespace ben_gear::session
