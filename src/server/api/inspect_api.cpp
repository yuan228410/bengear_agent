#include "server/api/inspect_api.hpp"
#include "server/session/pool.hpp"
#include "workspace/session.hpp"
#include "workspace/history_db.hpp"
#include "llm/conversation_history.hpp"
#include "agent/runtime/memory_context.hpp"
#include "acp/core/message.hpp"
#include "log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

/// 角色名转字符串
const char* role_name(acp::Role role) {
    switch (role) {
        case acp::Role::System:    return "system";
        case acp::Role::User:      return "user";
        case acp::Role::Assistant: return "assistant";
        case acp::Role::Tool:      return "tool";
        default:                   return "unknown";
    }
}

/// 从活跃会话中获取 history 快照
struct HistorySnapshot {
    std::string system_prompt;
    base::json::Json messages = base::json::Json::array();
    bool valid = false;
    bool active = false;  // 是否来自活跃会话
};

HistorySnapshot capture_from_active(SessionEntry& entry) {
    HistorySnapshot snap;
    if (!entry.session) return snap;

    auto& history = entry.session->history();
    snap.system_prompt = history.get_system_prompt();

    for (const auto& msg : history.messages()) {
        base::json::Json item;
        item["role"] = role_name(msg.role());
        item["content"] = msg.get_all_text();
        // 工具调用
        if (msg.role() == acp::Role::Assistant) {
            const auto& tool_calls = msg.get_tool_calls();
            if (!tool_calls.empty()) {
                base::json::Json tc_array = base::json::Json::array();
                for (const auto& tc : tool_calls) {
                    base::json::Json tc_obj;
                    tc_obj["id"] = tc.id;
                    tc_obj["name"] = tc.name;
                    tc_obj["args"] = tc.arguments.dump();
                    tc_array.push_back(std::move(tc_obj));
                }
                item["tool_calls"] = std::move(tc_array);
            }
        }
        // 工具结果
        if (msg.role() == acp::Role::Tool) {
            const auto& results = msg.get_tool_results();
            if (!results.empty()) {
                base::json::Json tr_array = base::json::Json::array();
                for (const auto& tr : results) {
                    base::json::Json tr_obj;
                    tr_obj["tool_call_id"] = tr.tool_call_id;
                    tr_obj["name"] = tr.name;
                    tr_obj["output"] = tr.output;
                    tr_array.push_back(std::move(tr_obj));
                }
                item["tool_results"] = std::move(tr_array);
            }
        }
        snap.messages.push_back(std::move(item));
    }

    snap.valid = true;
    snap.active = true;
    return snap;
}

/// 从数据库恢复消息历史（不活跃会话）
HistorySnapshot capture_from_db(workspace::HistoryDB& db, const std::string& session_id) {
    HistorySnapshot snap;
    auto msgs = db.load_session_chat_messages(session_id, 500);
    for (const auto& msg : msgs) {
        base::json::Json item;
        item["role"] = msg.value("role", "");
        item["content"] = msg.value("content", "");
        if (msg.contains("tool_name") && !msg.value("tool_name", "").empty()) {
            item["tool_name"] = msg.value("tool_name", "");
        }
        snap.messages.push_back(std::move(item));
    }
    snap.valid = true;
    snap.active = false;
    return snap;
}

} // namespace

void register_inspect_routes(Router& router, SessionPool& session_pool,
                              std::shared_ptr<workspace::HistoryDB> history_db) {

    // ── 查看系统提示词 ────────────────────────────────────
    router.add_route("GET", "/api/inspect/prompt",
        [&session_pool](const HttpRequest& req) {
            auto sid_it = req.query.find("session_id");
            if (sid_it == req.query.end())
                return HttpResponse::error(400, "missing session_id");

            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";

            // 通过 ContextBuilder 重新构建系统提示词（活跃会话）
            std::string system_prompt;
            bool found = false;
            session_pool.for_each_active([&](const std::string& sid,
                                              const std::string& /*user*/,
                                              const std::string& entry_ws,
                                              SessionEntry& entry) {
                if (found) return;
                if (sid != sid_it->second || entry_ws != ws) return;
                if (!entry.runtime) return;
                try {
                    auto& mem_ctx = entry.runtime->services().resolve_ref<agent::runtime::IMemoryContext>();
                    system_prompt = mem_ctx.builder()->build();
                    found = true;
                } catch (...) {
                    // IMemoryContext 不可用，回退到 history
                    if (entry.session) {
                        system_prompt = entry.session->history().get_system_prompt();
                        found = true;
                    }
                }
            });

            if (!found)
                return HttpResponse::error(404, "系统提示词仅在活跃会话中可用，请先打开该会话");

            base::json::Json result;
            result["system_prompt"] = system_prompt;
            return HttpResponse::ok(result.dump());
        });

    // ── 查看完整上下文 ────────────────────────────────────
    router.add_route("GET", "/api/inspect/context",
        [&session_pool, &history_db](const HttpRequest& req) {
            auto sid_it = req.query.find("session_id");
            if (sid_it == req.query.end())
                return HttpResponse::error(400, "missing session_id");

            auto ws = req.query.count("workspace") ? req.query.at("workspace") : "default";

            // 优先从活跃会话获取
            HistorySnapshot snap;
            std::string builder_prompt;  // 从 ContextBuilder 实时构建的系统提示词
            session_pool.for_each_active([&](const std::string& sid,
                                              const std::string& /*user*/,
                                              const std::string& entry_ws,
                                              SessionEntry& entry) {
                if (snap.valid) return;
                if (sid != sid_it->second || entry_ws != ws) return;
                snap = capture_from_active(entry);

                // 优先用 ContextBuilder 实时构建（不依赖 history 中是否有 system 消息）
                if (entry.runtime) {
                    try {
                        auto& mem_ctx = entry.runtime->services().resolve_ref<agent::runtime::IMemoryContext>();
                        builder_prompt = mem_ctx.builder()->build();
                    } catch (...) {}
                }
            });

            // 不活跃会话从数据库恢复
            if (!snap.valid && history_db) {
                snap = capture_from_db(*history_db, sid_it->second);
            }

            if (!snap.valid)
                return HttpResponse::error(404, "无法获取上下文");

            // 系统提示词优先级：ContextBuilder > history > 空
            base::json::Json result;
            result["system_prompt"] = !builder_prompt.empty() ? builder_prompt : snap.system_prompt;
            result["active"] = snap.active;
            result["messages"] = std::move(snap.messages);
            return HttpResponse::ok(result.dump());
        });

    log::info_fmt("API: inspect routes registered (2)");
}

} // namespace ben_gear::server
