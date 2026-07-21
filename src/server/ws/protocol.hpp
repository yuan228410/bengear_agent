#pragma once

#include <unordered_map>

#include <string>

namespace ben_gear::server {


/// WebSocket 消息协议（v1）：结构化传输，不绑定 UI 展示。
struct WsMessage {
    int version = 1;
    std::string type;
    std::string session_id;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, int> ints;
    std::unordered_map<std::string, double> doubles;
    std::string json_data;
    bool json_data_raw = false;

    std::string to_json() const;
    static WsMessage from_json(const std::string& json_str);

    // 客户端 -> 服务端
    static WsMessage chat(const std::string& session_id, const std::string& prompt);
    static WsMessage abort(const std::string& session_id);
    static WsMessage plan_start(const std::string& session_id, const std::string& data);
    static WsMessage plan_chat(const std::string& session_id, const std::string& data);
    static WsMessage plan_update_items(const std::string& session_id, const std::string& data);
    static WsMessage plan_select_option(const std::string& session_id, const std::string& data);
    static WsMessage plan_apply_choice(const std::string& session_id, const std::string& data);
    static WsMessage plan_apply_decision(const std::string& session_id, const std::string& data);
    static WsMessage plan_finalize(const std::string& session_id, const std::string& data);
    static WsMessage plan_confirm(const std::string& session_id, const std::string& data);
    static WsMessage plan_cancel(const std::string& session_id, const std::string& data);
    static WsMessage todo_update(const std::string& session_id, const std::string& data);
    static WsMessage permission_list(const std::string& session_id, const std::string& data);
    static WsMessage permission_approve(const std::string& session_id, const std::string& data);
    static WsMessage permission_deny(const std::string& session_id, const std::string& data);
    static WsMessage switch_session(const std::string& session_id, const std::string& workspace);
    static WsMessage rename(const std::string& session_id, const std::string& name);
    static WsMessage del(const std::string& session_id);
    static WsMessage ping();

    // 服务端 -> 客户端
    static WsMessage token(const std::string& session_id, const std::string& content);
    static WsMessage thinking(const std::string& session_id, int chars, double elapsed, const std::string& content = {});
    static WsMessage tool_call(const std::string& session_id, const std::string& name, const std::string& args);
    static WsMessage tool_result(const std::string& session_id, const std::string& name, const std::string& result, double elapsed);
    static WsMessage execution_event(const std::string& session_id, const std::string& data);
    static WsMessage plan_state(const std::string& session_id, const std::string& data);
    static WsMessage plan_delta(const std::string& session_id, const std::string& data);
    static WsMessage todo_state(const std::string& session_id, const std::string& data);
    static WsMessage todo_delta(const std::string& session_id, const std::string& data);
    static WsMessage permission_state(const std::string& session_id, const std::string& data);
    static WsMessage permission_result(const std::string& session_id, const std::string& data);
    static WsMessage done(const std::string& session_id, const std::string& usage_json, double total_seconds, double ttfb_seconds);
    static WsMessage done_with_outcome(const std::string& session_id, const std::string& usage_json, const std::string& outcome_json, double total_seconds, double ttfb_seconds);
    static WsMessage error_msg(const std::string& session_id, const std::string& message);
    static WsMessage error_msg(const std::string& session_id, const std::string& message, const std::string& outcome_json);
    static WsMessage connected(const std::string& session_id, const std::string& config_json);
    static WsMessage sessions(const std::string& sessions_json);
    static WsMessage pong();
};

} // namespace ben_gear::server
