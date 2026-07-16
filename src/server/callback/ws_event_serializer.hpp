#pragma once

#include "server/ws/handler.hpp"
#include "server/ws/protocol.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"
#include "capabilities/tool/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ben_gear::server {

/// Converts typed domain events to WsMessage wire format and sends them via WsHandler.
/// Owns the WsHandler reference; EventCollector delegates serialization + dispatch here.
class WsEventSerializer {
public:
    WsEventSerializer(std::shared_ptr<WsHandler> ws, std::string workspace);

    void send_token(std::string_view session_id, std::string_view token) const;
    void send_thinking(std::string_view session_id, std::string_view token) const;
    void send_tool_call(std::string_view session_id, const capabilities::tool::ToolCallRequest& call) const;
    void send_tool_result(std::string_view session_id, const capabilities::tool::ToolCallResult& result) const;
    void send_execution_event(std::string_view session_id, const orchestration::ExecutionEvent& event) const;
    void send_todo_state(std::string_view session_id, const orchestration::TodoState& state) const;
    void send_todo_delta(std::string_view session_id, const orchestration::TodoDelta& delta) const;

    /// Enrich a WsMessage with workspace metadata
    WsMessage enrich(WsMessage msg) const;

    bool alive() const;

    std::shared_ptr<WsHandler> handler() const { return ws_; }

private:
    void send(WsMessage msg) const;

    std::shared_ptr<WsHandler> ws_;
    std::string workspace_;
};

} // namespace ben_gear::server
