#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"

#include <string>

namespace ben_gear::server {

namespace container = base::container;

/// WebSocket 消息协议（v1）
struct WsMessage {
    int version = 1;
    container::String type;
    container::String session_id;
    container::Map<container::String, container::String> strings;
    container::Map<container::String, int> ints;
    container::Map<container::String, double> doubles;
    std::string json_data;

    std::string to_json() const;
    static WsMessage from_json(const std::string& json_str);

    // 客户端 -> 服务端
    static WsMessage chat(const container::String& session_id, const container::String& prompt);
    static WsMessage abort(const container::String& session_id);
    static WsMessage switch_session(const container::String& session_id, const container::String& workspace);
    static WsMessage rename(const container::String& session_id, const container::String& name);
    static WsMessage del(const container::String& session_id);
    static WsMessage ping();

    // 服务端 -> 客户端
    static WsMessage token(const container::String& session_id, const container::String& content);
    static WsMessage thinking(const container::String& session_id, int chars, double elapsed, const container::String& content = {});
    static WsMessage tool_call(const container::String& session_id, const container::String& name, const std::string& args);
    static WsMessage tool_result(const container::String& session_id, const container::String& name, const std::string& result, double elapsed);
    static WsMessage sub_agent(const container::String& session_id, const container::String& event_type, const std::string& data);
    static WsMessage done(const container::String& session_id, const std::string& usage_json, double total_seconds, double ttfb_seconds);
    static WsMessage done_with_outcome(const container::String& session_id, const std::string& usage_json, const std::string& outcome_json, double total_seconds, double ttfb_seconds);
    static WsMessage error_msg(const container::String& session_id, const container::String& message);
    static WsMessage error_msg(const container::String& session_id, const container::String& message, const std::string& outcome_json);
    static WsMessage connected(const container::String& session_id, const std::string& config_json);
    static WsMessage sessions(const std::string& sessions_json);
    static WsMessage pong();
};

} // namespace ben_gear::server
