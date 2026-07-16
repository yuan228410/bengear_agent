#include "server/callback/ws_event_serializer.hpp"
#include "base/net/event_loop.hpp"
#include "base/log/logger.hpp"
#include "orchestration/serializer.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

WsEventSerializer::WsEventSerializer(std::shared_ptr<WsHandler> ws, std::string workspace)
    : ws_(std::move(ws)), workspace_(std::move(workspace)) {}

void WsEventSerializer::send_token(std::string_view session_id, std::string_view token) const {
    send(WsMessage::token(std::string(session_id), std::string(token)));
}

void WsEventSerializer::send_thinking(std::string_view session_id, std::string_view token) const {
    send(WsMessage::thinking(std::string(session_id), static_cast<int>(token.size()), 0.0, std::string(token)));
}

void WsEventSerializer::send_tool_call(std::string_view session_id, const capabilities::tool::ToolCallRequest& call) const {
    send(WsMessage::tool_call(std::string(session_id), call.name, call.arguments.dump()));
}

void WsEventSerializer::send_tool_result(std::string_view session_id, const capabilities::tool::ToolCallResult& result) const {
    send(WsMessage::tool_result(std::string(session_id), result.name,
                                std::string(result.output.data(), result.output.size()), 0.0));
}

void WsEventSerializer::send_execution_event(std::string_view session_id,
                                              const orchestration::ExecutionEvent& event) const {
    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(std::string(session_id),
                                    std::string(payload.data(), payload.size())));
}

void WsEventSerializer::send_todo_state(std::string_view session_id,
                                         const orchestration::TodoState& state) const {
    auto payload = orchestration::to_json_string(state);
    send(WsMessage::todo_state(std::string(session_id),
                               std::string(payload.data(), payload.size())));
}

void WsEventSerializer::send_todo_delta(std::string_view session_id,
                                         const orchestration::TodoDelta& delta) const {
    auto payload = orchestration::to_json_string(delta);
    send(WsMessage::todo_delta(std::string(session_id),
                               std::string(payload.data(), payload.size())));
}

WsMessage WsEventSerializer::enrich(WsMessage msg) const {
    if (!workspace_.empty()) msg.strings[std::string("workspace")] = workspace_;
    return msg;
}

bool WsEventSerializer::alive() const { return ws_ && ws_->alive(); }

void WsEventSerializer::send(WsMessage msg) const {
    auto enriched = enrich(std::move(msg));
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("WsEventSerializer: ws not alive, dropping msg type={} session={}",
                      enriched.type.c_str(), enriched.session_id.c_str());
        return;
    }
    auto json = enriched.to_json();
    std::shared_ptr<WsHandler> handler = ws_;
    auto& loop = handler->loop();

    if (loop.is_loop_thread()) {
        handler->queue_send(std::move(json));
    } else {
        loop.submit_task([handler, json = std::move(json)]() mutable {
            if (handler && handler->alive()) {
                handler->queue_send(std::move(json));
            }
        });
    }
}

} // namespace ben_gear::server
