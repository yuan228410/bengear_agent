#include "server/ws/session_message_dispatcher.hpp"

#include "base/net/event_loop.hpp"
#include "orchestration/plan_parser.hpp"
#include "orchestration/serializer.hpp"

namespace ben_gear::server {

std::string json_field(const Json& json, std::string_view key) {
    return json.value(key, "");
}

int json_int_field(const Json& json, std::string_view key, int fallback) {
    return json.value(key, fallback);
}

bool msg_bool_field(const WsMessage& msg, std::string_view key, bool fallback) {
    auto it = msg.ints.find(std::string(key));
    if (it != msg.ints.end()) return it->second != 0;
    return fallback;
}

bool json_bool_field(const Json& json, std::string_view key, bool fallback) {
    return json.value(key, fallback);
}

Json parse_message_data(const WsMessage& msg, std::string& error) {
    if (msg.json_data.empty()) return Json::object();
    auto json = parse_json(std::string_view(msg.json_data.data(), msg.json_data.size()), error);
    if (!error.empty() || !json.is_object()) {
        if (error.empty()) error = "message data must be a JSON object";
        return Json();
    }
    return json;
}

void queue_ws(std::shared_ptr<WsHandler> ws, WsMessage msg) {
    if (!ws || !ws->alive()) return;
    auto json = msg.to_json();
    auto& loop = ws->loop();
    if (loop.is_loop_thread()) {
        ws->queue_send(std::move(json));
    } else {
        loop.submit_task([ws, json = std::move(json)]() mutable {
            if (ws && ws->alive()) ws->queue_send(std::move(json));
        });
    }
}

void emit_plan_state(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft) {
    auto payload = orchestration::to_json_string(draft);
    auto msg = WsMessage::plan_state(draft.session_id, std::string(payload.data(), payload.size()));
    if (!draft.workspace.empty()) msg.strings[std::string("workspace")] = draft.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

void emit_todo_state(std::shared_ptr<WsHandler> ws, const orchestration::TodoState& state) {
    auto payload = orchestration::to_json_string(state);
    auto msg = WsMessage::todo_state(state.session_id, std::string(payload.data(), payload.size()));
    if (!state.workspace.empty()) msg.strings[std::string("workspace")] = state.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

Json session_not_found_json() {
    return Json{{"success", false}, {"error_type", "session_not_found"}, {"message", "session not found"}};
}

void emit_plan_delta(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft, const Json& delta) {
    auto payload = delta.dump();
    auto msg = WsMessage::plan_delta(draft.session_id, payload);
    if (!draft.workspace.empty()) msg.strings[std::string("workspace")] = draft.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

void persist_plan_state(SessionEntry& entry) {
    auto payload = orchestration::to_json_string(entry.plan_manager.draft());
    const auto& draft = entry.plan_manager.draft();
    entry.runtime->history_db().save_session_state_async(draft.workspace, draft.session_id, std::string("plan"), payload);
}

void persist_todo_state(SessionEntry& entry) {
    auto payload = orchestration::to_json_string(entry.todo_manager.state());
    const auto& state = entry.todo_manager.state();
    entry.runtime->history_db().save_session_state_async(state.workspace, state.session_id, std::string("todo"), payload);
}

std::string build_execution_prompt(const orchestration::PlanDraft& plan) {
    std::string prompt =
        "Execute the approved final plan exactly. Use final_items and preserve every user-selected decision.\n"
        "Keep the visible TODO list accurate in real time: before starting each final plan item, call update_todo with that item status=running and progress=0; when the item completes, call update_todo with status=succeeded and progress=100; if it fails or blocks, call update_todo with status=failed or blocked and include result_summary. Do not wait until the whole plan is done to update TODOs.\n";
    prompt += "Final plan JSON:\n";
    auto json = orchestration::to_json_string(plan);
    prompt.append(json.data(), json.size());
    return std::string(prompt.c_str(), prompt.size());
}

orchestration::PlanFinalDraft build_local_final_draft(const orchestration::PlanDraft& draft) {
    orchestration::PlanFinalDraft final_draft;
    final_draft.summary = draft.title.empty() ? std::string("Approved plan ready for execution") : draft.title;
    final_draft.items = draft.items;
    final_draft.global_risks = draft.global_risks;
    final_draft.validation = draft.validation;
    final_draft.consistency_notes.push_back(std::string("Fast local synthesis used; user-selected decisions are preserved."));

    for (auto& item : final_draft.items) {
        if (item.decisions.empty()) continue;
        std::string desc(item.description.data(), item.description.size());
        bool wrote_header = false;
        for (const auto& decision : item.decisions) {
            std::string selected;
            if (!decision.custom_note.empty()) {
                selected = decision.custom_note;
            } else {
                for (const auto& choice : decision.choices) {
                    if (choice.id == decision.selected_choice_id) {
                        selected = choice.title.empty() ? choice.description : choice.title;
                        break;
                    }
                }
            }
            if (selected.empty()) continue;
            if (!wrote_header) {
                if (!desc.empty()) desc += " ";
                desc += "Selected decisions:";
                wrote_header = true;
            }
            desc += " ";
            desc.append(decision.title.data(), decision.title.size());
            desc += " = ";
            desc.append(selected.data(), selected.size());
            desc += ";";
        }
        item.description = std::string(desc.c_str(), desc.size());
    }
    return final_draft;
}

namespace {

bool is_continue_prompt(std::string_view prompt) {
    while (!prompt.empty() && (prompt.front() == ' ' || prompt.front() == '\n' || prompt.front() == '\t' || prompt.front() == '\r')) prompt.remove_prefix(1);
    while (!prompt.empty() && (prompt.back() == ' ' || prompt.back() == '\n' || prompt.back() == '\t' || prompt.back() == '\r' || prompt.back() == '.' || prompt.back() == '!' || prompt.back() == '?')) prompt.remove_suffix(1);
    return prompt == "继续" || prompt == "继续执行" || prompt == "继续进行" || prompt == "continue" || prompt == "resume";
}

} // namespace

std::string maybe_append_continue_context(std::string prompt, const orchestration::TodoManager& todo_manager) {
    if (todo_manager.empty() || !is_continue_prompt(std::string_view(prompt.data(), prompt.size()))) return prompt;
    prompt.append("\n\nResume the previous interrupted task using the current TODO state. Continue pending or blocked work, do not repeat succeeded work, and use update_todo to refine or update TODO items when useful.");
    return prompt;
}

} // namespace ben_gear::server

