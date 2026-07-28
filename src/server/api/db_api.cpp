#include "server/api/db_api.hpp"
#include "log/logger.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <string>

namespace ben_gear::server {

namespace {

/// 安全的 sqlite3 查询回调上下文
struct QueryCtx {
    Json rows = Json::array();
    Json columns = Json::array();
    bool has_columns = false;
};

/// sqlite3_exec 回调：收集列名和行数据
int collect_rows(void* data, int argc, char** argv, char** col_names) {
    auto* ctx = static_cast<QueryCtx*>(data);
    if (!ctx->has_columns) {
        for (int i = 0; i < argc; ++i) ctx->columns.push_back(col_names[i] ? col_names[i] : "");
        ctx->has_columns = true;
    }
    Json row = Json::array();
    for (int i = 0; i < argc; ++i) row.push_back(argv[i] ? argv[i] : "");
    ctx->rows.push_back(std::move(row));
    return 0;
}

/// 执行只读查询，返回 {columns, rows}
Json exec_query(sqlite3* db, const std::string& sql) {
    QueryCtx ctx;
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), collect_rows, &ctx, &err);
    if (rc != SQLITE_OK) {
        std::string err_msg = err ? err : "unknown error";
        sqlite3_free(err);
        Json result;
        result["error"] = err_msg;
        return result;
    }
    Json result;
    result["columns"] = std::move(ctx.columns);
    result["rows"] = std::move(ctx.rows);
    return result;
}

/// 获取数据库文件大小（字节）
int64_t db_file_size(const std::filesystem::path& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<int64_t>(size);
}

/// 格式化文件大小
std::string format_size(int64_t bytes) {
    if (bytes >= 1073741824) return std::to_string(bytes / 1073741824) + " GB";
    if (bytes >= 1048576) return std::to_string(bytes / 1048576) + " MB";
    if (bytes >= 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

/// 表名安全校验：只允许字母数字下划线
bool is_safe_table_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

/// SQL 字符串转义，防止注入
std::string sql_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    out += '\'';
    return out;
}

} // namespace

void register_db_routes(Router& router, std::shared_ptr<workspace::HistoryDB> db) {
    if (!db) {
        log::warn_fmt("DB API: history_db is null, skipping registration");
        return;
    }

    // 用户工作空间列表（用于筛选）
    router.add_route("GET", "/api/db/workspaces",
        [db](const HttpRequest& req) {
            auto user = sql_escape(req.username.empty() ? std::string("default") : req.username);
            auto db_path = db->db_path();
            sqlite3* raw = nullptr;
            int rc = sqlite3_open_v2(db_path.string().c_str(), &raw,
                                     SQLITE_OPEN_READONLY, nullptr);
            if (rc != SQLITE_OK) {
                if (raw) sqlite3_close(raw);
                return HttpResponse::error(500, "cannot open database");
            }
            auto result = exec_query(raw,
                "SELECT DISTINCT workspace FROM sessions WHERE user = " + user +
                " ORDER BY workspace;");
            sqlite3_close(raw);
            if (result.contains("error")) {
                return HttpResponse::error(500, result["error"].get<std::string>());
            }
            Json ws_list = Json::array();
            for (const auto& row : result["rows"]) {
                if (!row.empty()) ws_list.push_back(row[0]);
            }
            return HttpResponse::ok(ws_list.dump());
        });

    // 数据库概览：路径、大小、表列表（表行数按用户隔离）
    router.add_route("GET", "/api/db/info",
        [db](const HttpRequest& req) {
            auto db_path = db->db_path();
            sqlite3* raw = nullptr;
            int rc = sqlite3_open_v2(db_path.string().c_str(), &raw,
                                     SQLITE_OPEN_READONLY, nullptr);
            if (rc != SQLITE_OK) {
                if (raw) sqlite3_close(raw);
                return HttpResponse::error(500, "cannot open database");
            }

            // 查询所有用户表（排除 SQLite 内部表）
            auto tables_result = exec_query(raw,
                "SELECT name FROM sqlite_master WHERE type='table' "
                "AND name NOT LIKE 'sqlite_%' AND name NOT LIKE '%_fts' "
                "AND name NOT LIKE '%_fts_%' AND name != 'messages_fts' "
                "ORDER BY name;");

            if (tables_result.contains("error")) {
                sqlite3_close(raw);
                return HttpResponse::error(500, tables_result["error"].get<std::string>());
            }

            // 按用户统计各表行数
            auto user = sql_escape(req.username.empty() ? std::string("default") : req.username);

            Json table_list = Json::array();
            for (const auto& row : tables_result["rows"]) {
                if (row.empty()) continue;
                auto table_name = row[0].get<std::string>();
                Json item;
                item["name"] = table_name;

                // sessions 表：按 user 字段过滤
                if (table_name == "sessions") {
                    auto cnt = exec_query(raw, "SELECT COUNT(*) FROM sessions WHERE user = " + user + ";");
                    item["rows"] = (!cnt.contains("error") && !cnt["rows"].empty())
                        ? cnt["rows"][0][0].get<std::string>() : "0";
                }
                // messages 表：按 session_id 关联当前用户的 sessions
                else if (table_name == "messages") {
                    auto cnt = exec_query(raw,
                        "SELECT COUNT(*) FROM messages m "
                        "INNER JOIN sessions s ON m.session_id = s.session_id "
                        "WHERE s.user = " + user + ";");
                    item["rows"] = (!cnt.contains("error") && !cnt["rows"].empty())
                        ? cnt["rows"][0][0].get<std::string>() : "0";
                }
                // session_states 表：同理
                else if (table_name == "session_states") {
                    auto cnt = exec_query(raw,
                        "SELECT COUNT(*) FROM session_states st "
                        "INNER JOIN sessions s ON st.session_id = s.session_id "
                        "WHERE s.user = " + user + ";");
                    item["rows"] = (!cnt.contains("error") && !cnt["rows"].empty())
                        ? cnt["rows"][0][0].get<std::string>() : "0";
                }
                // 其他表（如 FTS 虚拟表）：不统计
                else {
                    item["rows"] = "-";
                }
                table_list.push_back(std::move(item));
            }
            sqlite3_close(raw);

            Json response;
            response["path"] = db_path.string();
            response["size"] = format_size(db_file_size(db_path));
            response["size_bytes"] = db_file_size(db_path);
            response["tables"] = std::move(table_list);
            return HttpResponse::ok(response.dump());
        });

    // 表结构 + 分页数据（按用户隔离，可按 workspace/session 筛选）
    router.add_route("GET", "/api/db/table/:name",
        [db](const HttpRequest& req) {
            auto it = req.params.find("name");
            if (it == req.params.end()) return HttpResponse::error(400, "missing table name");
            const auto& table_name = it->second;
            if (!is_safe_table_name(table_name)) return HttpResponse::error(400, "invalid table name");

            // 分页参数
            int page = 1;
            int limit = 50;
            auto page_it = req.query.find("page");
            if (page_it != req.query.end()) {
                try { page = std::max(1, std::stoi(page_it->second)); } catch (...) {}
            }
            auto limit_it = req.query.find("limit");
            if (limit_it != req.query.end()) {
                try { limit = std::max(1, std::min(500, std::stoi(limit_it->second))); } catch (...) {}
            }
            int offset = (page - 1) * limit;

            // 筛选参数
            auto user = sql_escape(req.username.empty() ? std::string("default") : req.username);
            auto ws_it = req.query.find("workspace");
            auto sid_it = req.query.find("session_id");
            // sessions 表直接用 workspace 过滤
            std::string ws_direct;
            if (ws_it != req.query.end() && !ws_it->second.empty()) {
                ws_direct = " AND workspace = " + sql_escape(ws_it->second);
            }
            // messages/session_states 通过 JOIN sessions 过滤，用 s.workspace
            std::string ws_join;
            if (ws_it != req.query.end() && !ws_it->second.empty()) {
                ws_join = " AND s.workspace = " + sql_escape(ws_it->second);
            }
            // session_id 筛选（messages 表用 m.session_id，session_states 用 st.session_id）
            std::string sid_msg;
            std::string sid_state;
            if (sid_it != req.query.end() && !sid_it->second.empty()) {
                sid_msg = " AND m.session_id = " + sql_escape(sid_it->second);
                sid_state = " AND st.session_id = " + sql_escape(sid_it->second);
            }

            auto db_path = db->db_path();
            sqlite3* raw = nullptr;
            int rc = sqlite3_open_v2(db_path.string().c_str(), &raw,
                                     SQLITE_OPEN_READONLY, nullptr);
            if (rc != SQLITE_OK) {
                if (raw) sqlite3_close(raw);
                return HttpResponse::error(500, "cannot open database");
            }

            // 表结构
            auto schema_result = exec_query(raw, "PRAGMA table_info(" + table_name + ");");

            // 构建按用户+筛选条件的查询 SQL
            std::string data_sql;
            std::string count_sql;

            if (table_name == "sessions") {
                count_sql = "SELECT COUNT(*) FROM sessions WHERE user = " + user + ws_direct + ";";
                data_sql = "SELECT * FROM sessions WHERE user = " + user + ws_direct +
                           " ORDER BY updated_at DESC LIMIT " + std::to_string(limit) +
                           " OFFSET " + std::to_string(offset) + ";";
            } else if (table_name == "messages") {
                count_sql = "SELECT COUNT(*) FROM messages m "
                            "INNER JOIN sessions s ON m.session_id = s.session_id "
                            "WHERE s.user = " + user + ws_join + sid_msg + ";";
                data_sql = "SELECT m.* FROM messages m "
                           "INNER JOIN sessions s ON m.session_id = s.session_id "
                           "WHERE s.user = " + user + ws_join + sid_msg +
                           " ORDER BY m.id LIMIT " + std::to_string(limit) +
                           " OFFSET " + std::to_string(offset) + ";";
            } else if (table_name == "session_states") {
                count_sql = "SELECT COUNT(*) FROM session_states st "
                            "INNER JOIN sessions s ON st.session_id = s.session_id "
                            "WHERE s.user = " + user + ws_join + sid_state + ";";
                data_sql = "SELECT st.* FROM session_states st "
                           "INNER JOIN sessions s ON st.session_id = s.session_id "
                           "WHERE s.user = " + user + ws_join + sid_state +
                           " LIMIT " + std::to_string(limit) +
                           " OFFSET " + std::to_string(offset) + ";";
            } else {
                sqlite3_close(raw);
                return HttpResponse::error(403, "access denied for table: " + table_name);
            }

            auto count_result = exec_query(raw, count_sql);
            auto data_result = exec_query(raw, data_sql);
            sqlite3_close(raw);

            Json response;
            response["table"] = table_name;
            response["page"] = page;
            response["limit"] = limit;

            int64_t total = 0;
            if (!count_result.contains("error") && !count_result["rows"].empty()) {
                try { total = std::stoll(count_result["rows"][0][0].get<std::string>()); } catch (...) {}
            }
            response["total"] = total;
            response["total_pages"] = limit > 0 ? (total + limit - 1) / limit : 0;

            if (schema_result.contains("error")) {
                response["schema_error"] = schema_result["error"];
            } else {
                response["schema"] = std::move(schema_result);
            }

            if (data_result.contains("error")) {
                response["data_error"] = data_result["error"];
            } else {
                response["data"] = std::move(data_result);
            }

            return HttpResponse::ok(response.dump());
        });

    log::info_fmt("API: db routes registered (2)");
}

} // namespace ben_gear::server
