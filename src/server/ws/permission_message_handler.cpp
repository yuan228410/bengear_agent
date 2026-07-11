#include "server/ws/permission_message_handler.hpp"

#include "server/ws/session_message_dispatcher.hpp"

namespace ben_gear::server {

bool handle_permission_ws_message(SessionPool& session_pool,
                                  std::shared_ptr<WsHandler> ws,
                                  const container::String& username,
                                  const container::String& workspace,
                                  const WsMessage& msg) {
    if (msg.type != "permission_list" && msg.type != "permission_approve" && msg.type != "permission_deny") return false;

    std::string error;
    auto data = parse_message_data(msg, error);
    if (!error.empty()) {
        queue_ws(ws, WsMessage::error_msg(msg.session_id, container::String(error.c_str())));
        return true;
    }

    auto entry = session_pool.get(msg.session_id, username, workspace);
    auto state = permission_state_for_entry(entry);
    if (msg.type == "permission_list") {
        emit_permission_state(ws, msg.session_id, workspace, state);
        return true;
    }

    Json result = session_not_found_json();
    if (entry && entry->agent && entry->agent->resources() && entry->agent->resources()->policy_engine()) {
        auto permission_id = json_field(data, "permission_id");
        if (permission_id.empty()) result = Json{{"success", false}, {"error_type", "bad_request"}, {"message", "missing permission_id"}};
        else if (msg.type == "permission_approve") result = entry->agent->resources()->policy_engine()->approve(permission_id, json_bool_field(data, "allow_session"));
        else result = entry->agent->resources()->policy_engine()->deny_pending(permission_id);
        state = permission_state_for_entry(entry);
    }
    Json payload{{"action", msg.type == "permission_approve" ? "approve" : "deny"}, {"result", result}, {"state", state}};
    auto response = WsMessage::permission_result(msg.session_id, payload.dump().to_std_string());
    response.strings[container::String("workspace")] = workspace;
    queue_ws(ws, std::move(response));
    return true;
}

} // namespace ben_gear::server
