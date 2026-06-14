#pragma once

#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/server/ws/handler.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/base/container/string.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

/// Server 模式回调 — AgentCallbacks → WS 推送
class ServerCallbacks : public agent::AgentCallbacks {
public:
    explicit ServerCallbacks(std::shared_ptr<WsHandler> ws,
                             const container::String& session_id,
                             const container::String& workspace);

    void on_token(std::string_view token) const override;
    void on_thinking(std::string_view token) const override;
    void on_tool_call(const llm::ToolCallRequest& call) const override;
    void on_tool_result(const llm::ToolCallResult& result) const override;
    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name = {},
                           int64_t context_length = 0) const override;
    void on_sub_agent_event(const agent::SubAgentEvent& event) const override;

    void set_session_id(const container::String& session_id);
    bool ws_alive() const;
    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;
    WsMessage enrich(WsMessage msg) const;

private:
    void send(const WsMessage& msg) const;
    std::string build_usage_json(const llm::TokenUsage& usage,
                                 std::string_view model_name,
                                 int64_t context_length) const;

    std::shared_ptr<WsHandler> ws_;
    container::String session_id_;
    container::String workspace_;
    mutable std::mutex send_mutex_;
    mutable std::mutex stats_mutex_;
    mutable bool has_response_stats_ = false;
    mutable std::string response_usage_json_;
    mutable llm::RequestLatency response_latency_;
};

} // namespace ben_gear::server
