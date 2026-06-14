#include "ben_gear/server/callback/server_callbacks.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::server {

ServerCallbacks::ServerCallbacks(std::shared_ptr<WsHandler> ws, const container::String& session_id, const container::String& workspace)
    : ws_(std::move(ws)), session_id_(session_id), workspace_(workspace) {}

void ServerCallbacks::on_token(std::string_view token) const {
    send(WsMessage::token(session_id_, container::String(token)));
}
void ServerCallbacks::on_thinking(std::string_view token) const {
    send(WsMessage::thinking(session_id_, static_cast<int>(token.size()), 0.0, container::String(token)));
}
void ServerCallbacks::on_tool_call(const llm::ToolCallRequest& call) const {
    send(WsMessage::tool_call(session_id_, call.name, call.arguments.dump()));
}
void ServerCallbacks::on_tool_result(const llm::ToolCallResult& result) const {
    send(WsMessage::tool_result(session_id_, result.name, std::string(result.output.data(), result.output.size()), 0.0));
}
std::string ServerCallbacks::build_usage_json(const llm::TokenUsage& usage,
                                               std::string_view model_name,
                                               int64_t context_length) const {
    std::string j = "{\"prompt_tokens\":" + std::to_string(usage.prompt_tokens)
        + ",\"completion_tokens\":" + std::to_string(usage.completion_tokens)
        + ",\"total_tokens\":" + std::to_string(usage.total_tokens);
    if (!model_name.empty()) j += ",\"model\":\"" + std::string(model_name) + "\"";
    if (context_length > 0) j += ",\"context_length\":" + std::to_string(context_length);
    j += "}";
    return j;
}

void ServerCallbacks::on_response_stats(const llm::TokenUsage& usage, const llm::RequestLatency& latency,
                                         std::string_view model_name, int64_t context_length) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(usage, model_name, context_length);
    response_latency_ = latency;
    has_response_stats_ = true;
}
void ServerCallbacks::on_sub_agent_event(const agent::SubAgentEvent& event) const {
    std::string d = "{\"type\":\"" + std::string(agent::to_string(event.type).c_str()) + "\",\"task_id\":\"" + std::string(event.task_id.c_str()) + "\"}";
    send(WsMessage::sub_agent(session_id_, agent::to_string(event.type), d));
}
void ServerCallbacks::set_session_id(const container::String& sid) { session_id_ = sid; }
bool ServerCallbacks::ws_alive() const { return ws_ && ws_->alive(); }
bool ServerCallbacks::has_response_stats() const {
    std::lock_guard lock(stats_mutex_);
    return has_response_stats_;
}
std::string ServerCallbacks::response_usage_json() const {
    std::lock_guard lock(stats_mutex_);
    return response_usage_json_.empty() ? std::string("{}") : response_usage_json_;
}
llm::RequestLatency ServerCallbacks::response_latency() const {
    std::lock_guard lock(stats_mutex_);
    return response_latency_;
}
WsMessage ServerCallbacks::enrich(WsMessage msg) const {
    if (!workspace_.empty()) msg.strings[container::String("workspace")] = workspace_;
    return msg;
}
void ServerCallbacks::send(const WsMessage& msg) const {
    auto enriched = enrich(msg);
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("ServerCallbacks: ws not alive, dropping msg type={} session={}",
                      enriched.type.c_str(), enriched.session_id.c_str());
        return;
    }
    auto json = enriched.to_json();
    auto& loop = ws_->loop();

    if (loop.is_loop_thread()) {
        // EventLoop 线程内：入写队列，单协程顺序 flush，帧不交错
        ws_->queue_send(std::move(json));
    } else {
        // 跨线程：sync_wait 阻塞等待完成
        std::lock_guard lock(send_mutex_);
        try { net::sync_wait(loop, ws_->send_text(json)); }
        catch (const std::exception& e) { log::debug_fmt("ServerCallbacks send failed: {}", e.what()); }
    }
}

} // namespace ben_gear::server
