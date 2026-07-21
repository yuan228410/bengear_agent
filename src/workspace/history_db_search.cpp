#include "workspace/history_db_impl.hpp"
#include <shared_mutex>
#include <mutex>

namespace ben_gear::workspace {

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

} // namespace ben_gear::workspace
