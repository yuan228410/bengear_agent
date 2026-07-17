#include "capabilities/tool/history_tools.hpp"

#include <vector>
#include "base/log/logger.hpp"
#include "workspace/history_db.hpp"
#include "workspace/types.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"

#include <chrono>
#include <ctime>
#include <string>
#include <regex>
#include <set>

namespace ben_gear::tools {

namespace container = base::container;

/// 解析时间字符串为 Unix 时间戳（秒）
/// 支持：ISO 日期(2024-01-01)、ISO 日期时间(2024-01-01T12:30:00)、相对时间(7d/1h)
/// 返回 0 表示解析失败
int64_t parse_time_string(const std::string& time_str) {
    if (time_str.empty()) return 0;

    // 相对时间：Nd 或 Nh
    {
        std::regex rel_re(R"(^(\d+)([dh])$)", std::regex::icase);
        std::smatch m;
        if (std::regex_match(time_str, m, rel_re)) {
            auto n = std::stoll(m[1].str());
            auto unit = m[2].str();
            auto now = std::chrono::system_clock::now();
            if (unit == "d" || unit == "D") {
                now -= std::chrono::hours(24 * n);
            } else {
                now -= std::chrono::hours(n);
            }
            return std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
        }
    }

    // ISO 日期时间：YYYY-MM-DDTHH:MM:SS
    {
        std::regex iso_re(R"(^(\d{4})-(\d{1,2})-(\d{1,2})[T ](\d{1,2}):(\d{1,2}):(\d{1,2})$)");
        std::smatch m;
        if (std::regex_match(time_str, m, iso_re)) {
            struct tm tm_buf{};
            tm_buf.tm_year = std::stoi(m[1].str()) - 1900;
            tm_buf.tm_mon = std::stoi(m[2].str()) - 1;
            tm_buf.tm_mday = std::stoi(m[3].str());
            tm_buf.tm_hour = std::stoi(m[4].str());
            tm_buf.tm_min = std::stoi(m[5].str());
            tm_buf.tm_sec = std::stoi(m[6].str());
            tm_buf.tm_isdst = -1;
            auto t = std::mktime(&tm_buf);
            if (t == -1) return 0;
            return static_cast<int64_t>(t);
        }
    }

    // ISO 日期：YYYY-MM-DD
    {
        std::regex iso_re(R"(^(\d{4})-(\d{1,2})-(\d{1,2})$)");
        std::smatch m;
        if (std::regex_match(time_str, m, iso_re)) {
            struct tm tm_buf{};
            tm_buf.tm_year = std::stoi(m[1].str()) - 1900;
            tm_buf.tm_mon = std::stoi(m[2].str()) - 1;
            tm_buf.tm_mday = std::stoi(m[3].str());
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            tm_buf.tm_isdst = -1;
            auto t = std::mktime(&tm_buf);
            if (t == -1) return 0;
            return static_cast<int64_t>(t);
        }
    }

    return 0;
}

/// 将 std::string 转为 std::string
std::string to_std(const std::string& s) {
    return s;
}

/// 注册历史会话删除工具
void register_history_tools(capabilities::tool::ToolRegistry& tools,
                                    workspace::HistoryDB& history_db,
                                    const workspace::WorkspaceContext& ws_ctx) {
    // 当前 workspace 名称和会话 ID
    std::string ws_name = ws_ctx.workspace_name.empty()
        ? std::string("default") : ws_ctx.workspace_name;
    std::string current_session_id = ws_ctx.session_id;

    tools.register_tool(
        std::string("delete_history"),
        std::string(
"Delete conversation history by condition. "
"IMPORTANT: You MUST call this tool with confirm=false first to get a preview. "
"Show the preview to the user and ask if they want to proceed. "
"Only call again with confirm=true AFTER the user explicitly says yes. "
"Do NOT skip the preview step. Do NOT set confirm=true without user confirmation.\n"
"Scopes: session (default, current session), all, before, after, keyword, messages_before, messages_keyword\n"
"Time: ISO date (2024-01-01) or relative (7d, 30d, 1h)"),
        {
            {"scope", capabilities::tool::ToolParameterSchema{
 .type = std::string("string"),
 .description = std::string(
 "Deletion scope: session (default, deletes current session), all, before, after, keyword, messages_before, messages_keyword")
}},
            {"before", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string(
                    "Time for 'before'/'messages_before' scope. ISO date or relative (7d, 1h)")
            }},
            {"after", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string(
                    "Time for 'after' scope. ISO date or relative time")
            }},
            {"keyword", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string(
                    "Keyword for 'keyword'/'messages_keyword' scope")
            }},
            {"session_id", capabilities::tool::ToolParameterSchema{
 .type = std::string("string"),
 .description = std::string(
 "Session ID. Defaults to current session. Only needed when deleting a different session.")
}},
            {"confirm", capabilities::tool::ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string(
                    "MUST be false on first call to preview. Only set true AFTER showing preview to user and receiving explicit confirmation. NEVER auto-set to true.")
            }},
        },
        [&history_db, &ws_ctx, ws_name, current_session_id](const Json& args) -> std::string {
            auto scope = args.value("scope", "");
            auto confirm = args.value("confirm", false);

            if (scope.empty()) {
 // 默认 scope 为 session（删除当前会话）
 scope = "session";
 }

            // 确保异步写入落盘
            history_db.flush();

            // ====== 会话级删除 ======

            if (scope == "all") {
                auto sessions = history_db.list_sessions(ws_ctx.username, ws_name);
                auto total_msgs = history_db.count_messages(ws_ctx.username, ws_name);

                if (!confirm) {
                    Json preview;
                    preview["scope"] = "all";
                    preview["workspace"] = to_std(ws_name);
                    preview["matching_sessions"] = static_cast<int>(sessions.size());
                    preview["total_messages"] = static_cast<int64_t>(total_msgs);
                    preview["action"] = to_std(std::string("Will delete all sessions in workspace '") + ws_name + "'");
                    Json sess_list = Json::array();
                    for (const auto& s : sessions) {
                        sess_list.push_back(s);
                    }
                    preview["sessions"] = sess_list;
                    return preview.dump();
                }

                int deleted = history_db.delete_all_sessions(ws_ctx.username, ws_name);
                auto remaining = history_db.count_messages(ws_ctx.username, ws_name);
                return std::string(Json{
                    {"scope", "all"},
                    {"workspace", to_std(ws_name)},
                    {"deleted_sessions", deleted},
                    {"remaining_messages", static_cast<int64_t>(remaining)}
                }.dump().c_str());
            }

            if (scope == "before") {
                auto before_str = args.value("before", "");
                auto before_ts = parse_time_string(before_str);
                if (before_ts == 0) {
                    return Json{{"error", to_std(std::string("Invalid 'before' time: ") + before_str)}}.dump();
                }

                if (!confirm) {
                    auto sessions = history_db.list_sessions(ws_ctx.username, ws_name);
                    Json matching = Json::array();
                    int match_count = 0;
                    for (const auto& s : sessions) {
                        auto updated = s.value("updated_at", "");
                        if (updated.size() >= 10) {
                            auto ts = parse_time_string(updated.substr(0, 10));
                            if (ts > 0 && ts < before_ts) {
                                matching.push_back(s);
                                match_count++;
                            }
                        }
                    }
                    Json result;
                    result["scope"] = "before";
                    result["before"] = before_str;
                    result["matching_sessions"] = match_count;
                    result["sessions"] = matching;
                    result["action"] = to_std(std::string("Will delete sessions updated before ") + before_str);
                    return result.dump();
                }

                int deleted = history_db.delete_sessions_before(ws_ctx.username, ws_name, before_ts);
                return std::string(Json{
                    {"scope", "before"},
                    {"before", before_str},
                    {"deleted_sessions", deleted}
                }.dump().c_str());
            }

            if (scope == "after") {
                auto after_str = args.value("after", "");
                auto after_ts = parse_time_string(after_str);
                if (after_ts == 0) {
                    return Json{{"error", to_std(std::string("Invalid 'after' time: ") + after_str)}}.dump();
                }

                if (!confirm) {
                    auto sessions = history_db.list_sessions(ws_ctx.username, ws_name);
                    Json matching = Json::array();
                    int match_count = 0;
                    for (const auto& s : sessions) {
                        auto updated = s.value("updated_at", "");
                        if (updated.size() >= 10) {
                            auto ts = parse_time_string(updated.substr(0, 10));
                            if (ts > 0 && ts > after_ts) {
                                matching.push_back(s);
                                match_count++;
                            }
                        }
                    }
                    Json result;
                    result["scope"] = "after";
                    result["after"] = after_str;
                    result["matching_sessions"] = match_count;
                    result["sessions"] = matching;
                    result["action"] = to_std(std::string("Will delete sessions updated after ") + after_str);
                    return result.dump();
                }

                int deleted = history_db.delete_sessions_after(ws_ctx.username, ws_name, after_ts);
                return std::string(Json{
                    {"scope", "after"},
                    {"after", after_str},
                    {"deleted_sessions", deleted}
                }.dump().c_str());
            }

            if (scope == "keyword") {
                auto keyword = args.value("keyword", "");
                if (keyword.empty()) {
                    return Json{{"error", "keyword is required for keyword scope"}}.dump();
                }
                std::string kw(keyword.c_str());

                if (!confirm) {
                    auto results = history_db.search(kw, ws_ctx.username, ws_name, 100);
                    std::set<std::string> session_ids;
                    for (const auto& r : results) {
                        if (r.contains("session_id")) {
                            session_ids.insert(r["session_id"].get<std::string>());
                        }
                    }
                    Json result;
                    result["scope"] = "keyword";
                    result["keyword"] = keyword;
                    result["matching_sessions"] = static_cast<int>(session_ids.size());
                    result["matching_messages"] = static_cast<int>(results.size());
                    result["action"] = to_std(std::string("Will delete sessions containing '") + keyword + "'");
                    return result.dump();
                }

                int deleted = history_db.delete_sessions_by_keyword(ws_ctx.username, ws_name, kw);
                return std::string(Json{
                    {"scope", "keyword"},
                    {"keyword", keyword},
                    {"deleted_sessions", deleted}
                }.dump().c_str());
            }

            if (scope == "session") {
                auto session_id = args.value("session_id", "");
                if (session_id.empty()) session_id = to_std(current_session_id);
                if (session_id.empty()) {
                    return Json{{"error", "session_id is required for session scope"}}.dump();
                }
                std::string sid(session_id.c_str());

                if (!confirm) {
                    auto msgs = history_db.load_session(sid);
                    Json result;
                    result["scope"] = "session";
                    result["session_id"] = session_id;
                    result["message_count"] = static_cast<int>(msgs.size());
                    result["action"] = to_std(std::string("Will delete session ") + session_id);
                    return result.dump();
                }

                bool ok = history_db.delete_session(sid);
                return std::string(Json{
                    {"scope", "session"},
                    {"session_id", session_id},
                    {"deleted", ok}
                }.dump().c_str());
            }

            // ====== 消息级删除 ======

            if (scope == "messages_before") {
                auto session_id = args.value("session_id", "");
                if (session_id.empty()) session_id = to_std(current_session_id);
                auto before_str = args.value("before", "");
                if (session_id.empty() || before_str.empty()) {
                    return Json{{"error", "session_id and before are required"}}.dump();
                }
                auto before_ts = parse_time_string(before_str);
                if (before_ts == 0) {
                    return Json{{"error", to_std(std::string("Invalid 'before' time: ") + before_str)}}.dump();
                }
                std::string sid(session_id.c_str());

                if (!confirm) {
                    auto total = history_db.count_session_messages(sid);
                    auto msgs = history_db.search_by_time(ws_ctx.username, ws_name, 0, before_ts, 10000);
                    int matching = 0;
                    for (const auto& m : msgs) {
                        if (m.contains("session_id") && m["session_id"].get<std::string>() == to_std(session_id)) {
                            matching++;
                        }
                    }
                    Json result;
                    result["scope"] = "messages_before";
                    result["session_id"] = session_id;
                    result["before"] = before_str;
                    result["matching_messages"] = matching;
                    result["total_in_session"] = static_cast<int64_t>(total);
                    result["action"] = to_std(std::string("Will delete messages before ") + before_str + " in session " + session_id);
                    return result.dump();
                }

                int deleted = history_db.delete_messages_before(sid, before_ts);
                auto remaining = history_db.count_session_messages(sid);
                return std::string(Json{
                    {"scope", "messages_before"},
                    {"session_id", session_id},
                    {"before", before_str},
                    {"deleted_messages", deleted},
                    {"remaining_in_session", static_cast<int64_t>(remaining)}
                }.dump().c_str());
            }

            if (scope == "messages_keyword") {
                auto session_id = args.value("session_id", "");
                if (session_id.empty()) session_id = to_std(current_session_id);
                auto keyword = args.value("keyword", "");
                if (session_id.empty() || keyword.empty()) {
                    return Json{{"error", "session_id and keyword are required"}}.dump();
                }
                std::string sid(session_id.c_str());
                std::string kw(keyword.c_str());

                if (!confirm) {
                    auto total = history_db.count_session_messages(sid);
                    auto results = history_db.search(kw, ws_ctx.username, ws_name, 1000);
                    int matching = 0;
                    for (const auto& r : results) {
                        if (r.contains("session_id") && r["session_id"].get<std::string>() == to_std(session_id)) {
                            matching++;
                        }
                    }
                    Json result;
                    result["scope"] = "messages_keyword";
                    result["session_id"] = session_id;
                    result["keyword"] = keyword;
                    result["matching_messages"] = matching;
                    result["total_in_session"] = static_cast<int64_t>(total);
                    result["action"] = to_std(std::string("Will delete messages containing '") + keyword + "' in session " + session_id);
                    return result.dump();
                }

                int deleted = history_db.delete_messages_by_keyword(sid, kw);
                auto remaining = history_db.count_session_messages(sid);
                return std::string(Json{
                    {"scope", "messages_keyword"},
                    {"session_id", session_id},
                    {"keyword", keyword},
                    {"deleted_messages", deleted},
                    {"remaining_in_session", static_cast<int64_t>(remaining)}
                }.dump().c_str());
            }

            return Json{{"error", to_std(std::string("Unknown scope: ") + std::string(scope))}}.dump();
        }
    );

    log::info_fmt("registered history tools");
}

}  // namespace ben_gear::tools
