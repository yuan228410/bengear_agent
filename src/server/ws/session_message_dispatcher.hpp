#pragma once

#include "orchestration/plan.hpp"
#include "orchestration/todo.hpp"
#include "server/session/pool.hpp"
#include "server/ws/handler.hpp"
#include "server/ws/protocol.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ben_gear::server {

container::String json_field(const Json& json, std::string_view key);
int json_int_field(const Json& json, std::string_view key, int fallback = 0);
bool msg_bool_field(const WsMessage& msg, std::string_view key, bool fallback = false);
bool json_bool_field(const Json& json, std::string_view key, bool fallback = false);
Json parse_message_data(const WsMessage& msg, std::string& error);

void queue_ws(std::shared_ptr<WsHandler> ws, WsMessage msg);
void emit_plan_state(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft);
void emit_todo_state(std::shared_ptr<WsHandler> ws, const orchestration::TodoState& state);
void emit_permission_state(std::shared_ptr<WsHandler> ws,
                           const container::String& session_id,
                           const container::String& workspace,
                           const Json& state);
void emit_plan_delta(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft, const Json& delta);

Json session_not_found_json();
Json permission_unavailable_json();
Json permission_state_for_entry(const std::shared_ptr<SessionEntry>& entry);

void persist_plan_state(SessionEntry& entry);
void persist_todo_state(SessionEntry& entry);

container::String build_execution_prompt(const orchestration::PlanDraft& plan);
orchestration::PlanFinalDraft build_local_final_draft(const orchestration::PlanDraft& draft);
container::String maybe_append_continue_context(container::String prompt, const orchestration::TodoManager& todo_manager);

} // namespace ben_gear::server
