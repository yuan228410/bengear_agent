#include "workspace/history_db_impl.hpp"
#include <shared_mutex>
#include <mutex>

#include <set>

namespace ben_gear::workspace {

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

    static const char* type_names[] = {"main", "sub_agent"};
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

    static const char* type_names[] = {"main", "sub_agent"};
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

    const char* ep_sql = "DELETE FROM episodes WHERE session_id=?";
    rc = sqlite3_prepare_v2(impl_->db, ep_sql, -1, &stmt, nullptr);
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

} // namespace ben_gear::workspace
