#include "workspace/history_db.hpp"
#include "workspace/history_db_impl.hpp"
#include "log/logger.hpp"

namespace ben_gear::workspace {

void HistoryDB::append_episode(const std::string& session_id,
                                const std::string& date,
                                const std::string& content) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = "INSERT INTO episodes(session_id, date, content, created_at) VALUES(?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log::error_fmt("HistoryDB append_episode prepare failed: {}", sqlite3_errmsg(impl_->db));
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        log::error_fmt("HistoryDB append_episode step failed: {}", sqlite3_errmsg(impl_->db));
    }
}

std::string HistoryDB::read_episode(const std::string& session_id,
                                     const std::string& date) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = "SELECT content FROM episodes WHERE session_id=? AND date=? ORDER BY id";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* text = sqlite3_column_text(stmt, 0);
        if (text) {
            if (!result.empty()) result += "\n\n";
            result += reinterpret_cast<const char*>(text);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> HistoryDB::read_episodes_range(
    const std::string& session_id,
    const std::string& from_date,
    const std::string& to_date) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql =
        "SELECT content FROM episodes WHERE session_id=? AND date>=? AND date<=? ORDER BY date, id";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, from_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, to_date.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<std::string> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* text = sqlite3_column_text(stmt, 0);
        if (text) {
            results.emplace_back(reinterpret_cast<const char*>(text));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Json> HistoryDB::list_episodes(const std::string& session_id) {
    std::shared_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql =
        "SELECT date, length(content) as size, created_at FROM episodes WHERE session_id=? ORDER BY date DESC, id DESC";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Json> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Json e;
        auto* date = sqlite3_column_text(stmt, 0);
        e["date"] = date ? reinterpret_cast<const char*>(date) : "";
        e["size"] = sqlite3_column_int(stmt, 1);
        e["created_at"] = Impl::format_ts(sqlite3_column_int64(stmt, 2));
        results.push_back(e);
    }
    sqlite3_finalize(stmt);
    return results;
}

bool HistoryDB::delete_episode(const std::string& session_id, const std::string& date) {
    std::unique_lock<std::shared_mutex> lock(impl_->rw_mutex);

    const char* sql = "DELETE FROM episodes WHERE session_id=? AND date=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

}  // namespace ben_gear::workspace
