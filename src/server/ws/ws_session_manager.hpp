#pragma once

#include "base/config/settings.hpp"
#include "base/net/task.hpp"
#include "agent/runtime/application/workspace_resolver.hpp"
#include "orchestration/todo.hpp"
#include "server/session/pool.hpp"
#include "server/core/event_bridge.hpp"

#include <functional>
#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear {

namespace server {

class EventBridge;


/// WS plan chat revision request (moved from Server)
struct PlanChatRequest {
    std::string mode;
    int revision = 0;
    std::string note;
    std::string custom_idea;
    std::string item_id;
    std::string decision_id;
};

/// Manages a WebSocket connection's message dispatch and session lifecycle.
/// Extracted from Server to decouple WS protocol handling from HTTP routing.
class WsSessionManager {
public:
    WsSessionManager(config::Settings settings,
                     SessionPool& session_pool,
                     application::WorkspaceResolver& resolver);

    /// Run the WS read loop for a connected handler. Blocks until the connection closes.
    net::Task<void> run_ws(std::shared_ptr<WsHandler> ws,
                           const std::string& username);
    /// 消息分发（命令注册表查找 + 委托）
    void dispatch(std::shared_ptr<WsHandler> ws, const std::string& username,
                  std::string_view message);

    // ---- 命令 handler ----
    using CmdFn = void (WsSessionManager::*)(std::shared_ptr<WsHandler>, const WsMessage&,
                                              const std::string&, std::shared_ptr<SessionEntry>);
    void cmd_chat(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                  const std::string& username, std::shared_ptr<SessionEntry> entry);
    void cmd_switch(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                    const std::string& username, std::shared_ptr<SessionEntry> entry);
    void cmd_plan(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                  const std::string& username, std::shared_ptr<SessionEntry> entry);
    void cmd_abort(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                   const std::string& username, std::shared_ptr<SessionEntry> entry);
    void cmd_ping(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                  const std::string& username, std::shared_ptr<SessionEntry> entry);

private:
    std::shared_ptr<SessionEntry> get_or_create_session(
        const std::string& session_id, const std::string& username,
        const std::string& workspace);

    void on_ws_message(std::shared_ptr<WsHandler> ws, const std::string& username,
                       std::string_view message);

    net::Task<void> handle_ws_chat(std::shared_ptr<WsHandler> ws,
                                    std::shared_ptr<EventBridge> event_sink,
                                    std::string session_id, std::string prompt,
                                    std::shared_ptr<SessionEntry> entry,
                                    bool persist_user_message = true);
    net::Task<void> handle_ws_plan_start(std::shared_ptr<WsHandler> ws,
                                          std::string session_id,
                                          std::string prompt,
                                          std::string note,
                                          std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_chat(std::shared_ptr<WsHandler> ws,
                                         std::string session_id,
                                         PlanChatRequest request,
                                         std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_update_items(std::shared_ptr<WsHandler> ws,
                                                 std::string session_id,
                                                 std::vector<orchestration::PlanItem> items,
                                                 std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_select_option(std::shared_ptr<WsHandler> ws,
                                                  std::string session_id,
                                                  std::string option_id,
                                                  int revision,
                                                  std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_apply_decision(std::shared_ptr<WsHandler> ws,
                                                   std::string session_id,
                                                   orchestration::PlanDecisionPatch patch,
                                                   std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_finalize(std::shared_ptr<WsHandler> ws,
                                             std::string session_id,
                                             int revision,
                                             std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_confirm(std::shared_ptr<WsHandler> ws,
                                            std::shared_ptr<EventBridge> event_sink,
                                            std::string session_id,
                                            int revision,
                                            bool has_items,
                                            std::vector<orchestration::PlanItem> items,
                                            std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_cancel(std::shared_ptr<WsHandler> ws,
                                           std::string session_id,
                                           std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_todo_update(std::shared_ptr<WsHandler> ws,
                                           orchestration::TodoItem item,
                                           std::shared_ptr<SessionEntry> entry);

    config::Settings settings_;
    SessionPool& session_pool_;
    application::WorkspaceResolver& resolver_;
    std::unordered_map<std::string, CmdFn> commands_;
};

} // namespace server
} // namespace ben_gear
