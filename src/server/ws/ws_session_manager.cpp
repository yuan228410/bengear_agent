#include "server/ws/ws_session_manager.hpp"
#include <mutex>

#include "server/ws/session_message_dispatcher.hpp"
#include "server/core/event_bridge.hpp"
// (absorbed by event_bridge.hpp)
#include "workspace/resolver.hpp"
#include "workspace/types.hpp"

#include "log/logger.hpp"
#include "net/cancel.hpp"
#include "net/event_loop.hpp"
#include "llm/run_outcome.hpp"
#include "orchestration/plan_parser.hpp"
#include "orchestration/serializer.hpp"
#include "domain/errors.hpp"

#include "llm/chat.hpp"
#include "llm/usage.hpp"
#include "llm/provider_client.hpp"
#include "capabilities/tool/manager.hpp"
#include "acp/core/message.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::server {

WsSessionManager::WsSessionManager(config::Settings settings,
                                   SessionPool& session_pool,
                                   workspace::WorkspaceResolver& resolver)
    : settings_(std::move(settings)),
      session_pool_(session_pool),
      resolver_(resolver) {
    // 命令注册表：O(1) 查找
    commands_ = {
        {"chat",    &WsSessionManager::cmd_chat},
        {"switch",  &WsSessionManager::cmd_switch},
        {"abort",   &WsSessionManager::cmd_abort},
        {"ping",    &WsSessionManager::cmd_ping},
        {"plan_start",            &WsSessionManager::cmd_plan},
        {"plan_chat",             &WsSessionManager::cmd_plan},
        {"plan_update_items",     &WsSessionManager::cmd_plan},
        {"plan_select_option",    &WsSessionManager::cmd_plan},
        {"plan_apply_choice",     &WsSessionManager::cmd_plan},
        {"plan_apply_decision",   &WsSessionManager::cmd_plan},
        {"plan_finalize",         &WsSessionManager::cmd_plan},
        {"plan_confirm",          &WsSessionManager::cmd_plan},
        {"plan_cancel",           &WsSessionManager::cmd_plan},
        {"todo_update",           &WsSessionManager::cmd_plan},
    };
}
std::shared_ptr<SessionEntry> WsSessionManager::get_or_create_session(
    const std::string& session_id, const std::string& username, const std::string& workspace) {
    auto project_path = resolver_.project_path_for(username, workspace);
    auto tier_paths = resolver_.tier_paths_for(username, workspace);
    auto ws_ctx = workspace::WorkspaceContext{
        tier_paths, workspace, project_path, username, session_id};
    return session_pool_.get_or_create(session_id, username, workspace, settings_, ws_ctx);
}

net::Task<void> WsSessionManager::run_ws(std::shared_ptr<WsHandler> ws,
                                          const std::string& username) {
    log::info_fmt("WsSessionManager: run_ws starting user={} alive={}", username, ws->alive());
    co_await ws->read_loop(
        [this, ws, username](std::string_view msg) { dispatch(ws, username, msg); },
        [username]() { log::info_fmt("WsSessionManager: WS disconnected user={}", username); });
    log::info_fmt("WsSessionManager: run_ws exiting user={}", username);
}

void WsSessionManager::dispatch(std::shared_ptr<WsHandler> ws, const std::string& username,
                                 std::string_view message) {
    auto msg = WsMessage::from_json(std::string(message));
    auto workspace = settings_.workspace_name;
    auto wit = msg.strings.find("workspace");
    if (wit != msg.strings.end() && !wit->second.empty()) workspace = wit->second;

    auto entry = get_or_create_session(msg.session_id, username, workspace);
    auto it = commands_.find(msg.type);
    if (it != commands_.end()) {
        (this->*(it->second))(ws, msg, username, entry);
    } else {
        log::warn_fmt("WsSessionManager: unknown msg type={}", msg.type);
    }
}

void WsSessionManager::cmd_chat(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                                 const std::string& username, std::shared_ptr<SessionEntry> entry) {
    auto pit = msg.strings.find("prompt");
    if (pit == msg.strings.end()) return;
    auto prompt = maybe_append_continue_context(pit->second, entry->todo_manager);
    auto workspace = msg.strings.count("workspace") ? msg.strings.at("workspace") : settings_.workspace_name;
    log::info_fmt("WsSessionManager: cmd_chat session={} prompt_len={} prompt_preview={} workspace={}", msg.session_id, prompt.size(), prompt.substr(0, 30), workspace);
    
    // 复用 session 级别的 EventBridge（已存在则更新开关状态）
    if (!entry->event_bridge) {
        entry->event_bridge = std::make_shared<EventBridge>(
            ws, msg.session_id, workspace, username,
            msg_bool_field(msg, "include_thinking"),
            msg_bool_field(msg, "include_tool_calls"),
            &entry->todo_manager, entry->runtime->services().resolve<workspace::HistoryDB>());
        entry->event_bridge->set_state_mutex(&entry->state_mutex);
    } else {
        // 页面刷新后重连：更新 ws 引用，避免持有已断开的旧 ws 导致 token 丢失
        entry->event_bridge->update_ws(ws);
        entry->event_bridge->set_include_options(
            msg_bool_field(msg, "include_thinking"),
            msg_bool_field(msg, "include_tool_calls"));
    }
    auto chat_ctx = entry->runtime->services().resolve<net::IoContext>();
    net::fire_and_forget(chat_ctx->loop(),
        handle_ws_chat(ws, entry->event_bridge, entry->session->session_id(), std::move(prompt), entry));
}

void WsSessionManager::cmd_switch(std::shared_ptr<WsHandler> ws, const WsMessage&,
                                   const std::string&, std::shared_ptr<SessionEntry> entry) {
    emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    emit_todo_state(ws, entry->todo_manager.state());
}

void WsSessionManager::cmd_plan(std::shared_ptr<WsHandler> ws, const WsMessage& msg,
                                 const std::string& username, std::shared_ptr<SessionEntry> entry) {
    std::string error;
    auto data = parse_message_data(msg, error);
    if (!error.empty()) { queue_ws(ws, WsMessage::error_msg(msg.session_id, error)); return; }
    auto workspace = msg.strings.count("workspace") ? msg.strings.at("workspace") : settings_.workspace_name;
    
    // 复用 session 级别的 EventBridge（已存在则更新开关状态）
    if (!entry->event_bridge) {
        entry->event_bridge = std::make_shared<EventBridge>(
            ws, msg.session_id, workspace, username,
            json_bool_field(data, "include_thinking"),
            json_bool_field(data, "include_tool_calls"),
            &entry->todo_manager, entry->runtime->services().resolve<workspace::HistoryDB>());
        entry->event_bridge->set_state_mutex(&entry->state_mutex);
    } else {
        entry->event_bridge->update_ws(ws);
        entry->event_bridge->set_include_options(
            json_bool_field(data, "include_thinking"),
            json_bool_field(data, "include_tool_calls"));
    }
    auto chat_ctx = entry->runtime->services().resolve<net::IoContext>();
    auto& loop = chat_ctx->loop();

    if (msg.type == "plan_start") {
        net::fire_and_forget(loop, handle_ws_plan_start(ws, entry->session->session_id(),
            json_field(data, "prompt"), json_field(data, "note"), entry));
    } else if (msg.type == "plan_chat") {
        PlanChatRequest req;
        req.mode = json_field(data, "mode"); if (req.mode.empty()) req.mode = "revise";
        req.revision = json_int_field(data, "revision");
        req.note = json_field(data, "note"); if (req.note.empty()) req.note = json_field(data, "prompt");
        req.custom_idea = json_field(data, "custom_idea");
        req.item_id = json_field(data, "item_id");
        req.decision_id = json_field(data, "decision_id");
        net::fire_and_forget(loop, handle_ws_plan_chat(ws, entry->session->session_id(), std::move(req), entry));
    } else if (msg.type == "plan_update_items") {
        std::vector<orchestration::PlanItem> items;
        auto raw_items = data["items"];
        if (raw_items.is_array()) for (size_t i = 0; i < raw_items.size(); ++i) items.push_back(orchestration::plan_item_from_json(raw_items[i]));
        net::fire_and_forget(loop, handle_ws_plan_update_items(ws, entry->session->session_id(), std::move(items), entry));
    } else if (msg.type == "plan_select_option") {
        net::fire_and_forget(loop, handle_ws_plan_select_option(ws, entry->session->session_id(),
            json_field(data, "option_id"), json_int_field(data, "revision"), entry));
    } else if (msg.type == "plan_apply_choice" || msg.type == "plan_apply_decision") {
        orchestration::PlanDecisionPatch patch;
        patch.revision = json_int_field(data, "revision");
        patch.item_id = json_field(data, "item_id");
        patch.decision_id = json_field(data, "decision_id");
        patch.choice_id = json_field(data, "choice_id");
        patch.custom_note = json_field(data, "custom_note");
        net::fire_and_forget(loop, handle_ws_plan_apply_decision(ws, entry->session->session_id(), std::move(patch), entry));
    } else if (msg.type == "plan_finalize") {
        net::fire_and_forget(loop, handle_ws_plan_finalize(ws, entry->session->session_id(), json_int_field(data, "revision"), entry));
    } else if (msg.type == "plan_confirm") {
        std::vector<orchestration::PlanItem> items;
        auto raw_items = data["items"];
        bool has_items = raw_items.is_array();
        if (has_items) for (size_t i = 0; i < raw_items.size(); ++i) items.push_back(orchestration::plan_item_from_json(raw_items[i]));
        net::fire_and_forget(loop, handle_ws_plan_confirm(ws, entry->event_bridge, entry->session->session_id(),
            json_int_field(data, "revision"), has_items, std::move(items), entry));
    } else if (msg.type == "plan_cancel") {
        net::fire_and_forget(loop, handle_ws_plan_cancel(ws, entry->session->session_id(), entry));
    } else if (msg.type == "todo_update") {
        net::fire_and_forget(loop, handle_ws_todo_update(ws, orchestration::todo_item_from_json(data["item"]), entry));
    }
}

void WsSessionManager::cmd_abort(std::shared_ptr<WsHandler>, const WsMessage& msg,
                                  const std::string&, std::shared_ptr<SessionEntry>) {
    auto workspace = msg.strings.count("workspace") ? msg.strings.at("workspace") : settings_.workspace_name;
    log::info_fmt("WsSessionManager: abort session={}", msg.session_id);
    if (!session_pool_.cancel(msg.session_id, msg.strings.count("username") ? msg.strings.at("username") : "default", workspace)) {
        log::debug_fmt("WsSessionManager: abort ignored session={} (not running)", msg.session_id);
    }
}

void WsSessionManager::cmd_ping(std::shared_ptr<WsHandler> ws, const WsMessage&,
                                 const std::string&, std::shared_ptr<SessionEntry>) {
    ws->queue_send_urgent(WsMessage::pong().to_json());
}

net::Task<void> WsSessionManager::handle_ws_plan_start(std::shared_ptr<WsHandler> ws,
                                                        std::string session_id,
                                                        std::string prompt,
                                                        std::string note,
                                                        std::shared_ptr<SessionEntry> entry) {
    if (prompt.empty()) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string("plan prompt is empty")));
        co_return;
    }
    orchestration::PlanCommand command;
    command.session_id = session_id;
    command.workspace = entry->session->workspace_context().workspace_name;
    command.prompt = prompt;
    command.note = note;
    {
        std::lock_guard state_lock(entry->state_mutex);
        entry->runtime->services().resolve<orchestration::PlanManager>()->start(command);
        entry->session->persist_message(std::string("user"), prompt, *entry->runtime->services().resolve<workspace::HistoryDB>());
        entry->session->persist_message(std::string("plan_anchor"), std::string(), *entry->runtime->services().resolve<workspace::HistoryDB>());
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }

    std::string previous_error;
    std::string previous_output;
    orchestration::PlanParseResult parsed;
    auto& agent_loop = entry->runtime->services().resolve<net::IoContext>()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto user_prompt = orchestration::build_plan_options_prompt(prompt, note, previous_error, previous_output);
        auto result = co_await entry->runtime->services().resolve_ref<llm::ProviderClient>().chat_simple(agent_loop,
            "Return structured JSON only for the web plan option review state.", user_prompt);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? std::string("LLM request failed") : result.error_message;
            continue;
        }
        parsed = orchestration::parse_plan_options_text(std::string_view(result.text.data(), result.text.size()), session_id, command.workspace, prompt);
        if (parsed.ok) break;
        previous_error = parsed.error;
    }

    {
        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->runtime->services().resolve<orchestration::PlanManager>()->mark_failed(previous_error.empty() ? std::string("failed to parse plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
            co_return;
        }

        parsed.draft.plan_id = entry->runtime->services().resolve<orchestration::PlanManager>()->draft().plan_id;
        entry->runtime->services().resolve<orchestration::PlanManager>()->restore(std::move(parsed.draft));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
}

net::Task<void> WsSessionManager::handle_ws_plan_chat(std::shared_ptr<WsHandler> ws,
                                                       std::string session_id,
                                                       PlanChatRequest request,
                                                       std::shared_ptr<SessionEntry> entry) {
    enum class RevisionKind { options, detail, final };

    auto feedback = request.custom_idea.empty() ? request.note : request.custom_idea;
    if (feedback.empty()) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string("plan revision feedback is empty")));
        co_return;
    }

    uint64_t request_id = 0;
    RevisionKind kind = RevisionKind::options;
    orchestration::PlanDraft snapshot;
    try {
        std::lock_guard state_lock(entry->state_mutex);
        const auto& current = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();
        if (request.mode == "reject_options") {
            if (current.stage != orchestration::PlanStage::option_review) throw domain::AppError::invalid_argument("STATE", "plan options can only be revised during option review");
            kind = RevisionKind::options;
        } else if (request.mode == "reject_decision") {
            if (current.stage != orchestration::PlanStage::decision_review && current.stage != orchestration::PlanStage::final_review) {
                throw domain::AppError::invalid_argument("STATE", "plan decisions can only be revised during decision review");
            }
            if (request.item_id.empty() || request.decision_id.empty()) throw domain::AppError::invalid_argument("MISSING_TARGET", "plan decision revision target is empty");
            kind = RevisionKind::detail;
        } else if (request.mode == "revise_final") {
            if (current.stage != orchestration::PlanStage::final_review) throw domain::AppError::invalid_argument("STATE", "final plan can only be revised during final review");
            kind = RevisionKind::final;
        } else {
            if (current.stage == orchestration::PlanStage::option_review) kind = RevisionKind::options;
            else if (current.stage == orchestration::PlanStage::decision_review) kind = RevisionKind::detail;
            else if (current.stage == orchestration::PlanStage::final_review) kind = RevisionKind::final;
            else throw domain::AppError::invalid_argument("STATE", "plan cannot be revised in the current stage");
        }
        request_id = entry->runtime->services().resolve<orchestration::PlanManager>()->begin_chat_revision(request.revision);
        snapshot = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();
        persist_plan_state(*entry);
        emit_plan_state(ws, snapshot);
    } catch (const std::exception& e) {
        std::lock_guard state_lock(entry->state_mutex);
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
        co_return;
    }

    std::string previous_error;
    std::string previous_output;
    orchestration::PlanParseResult parsed;
    auto& agent_loop = entry->runtime->services().resolve<net::IoContext>()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string user_prompt;
        if (kind == RevisionKind::options) {
            user_prompt = orchestration::build_plan_options_revision_prompt(snapshot, feedback, previous_error, previous_output);
        } else if (kind == RevisionKind::detail) {
            user_prompt = orchestration::build_plan_decision_revision_prompt(snapshot, request.item_id, request.decision_id, feedback, previous_error, previous_output);
        } else {
            user_prompt = orchestration::build_plan_final_revision_prompt(snapshot, feedback, previous_error, previous_output);
        }
        auto result = co_await entry->runtime->services().resolve_ref<llm::ProviderClient>().chat_simple(agent_loop,
            "Revise the structured plan and return JSON only.", user_prompt);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? std::string("LLM request failed") : result.error_message;
            continue;
        }
        if (kind == RevisionKind::options) {
            parsed = orchestration::parse_plan_options_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective);
        } else if (kind == RevisionKind::detail) {
            parsed = orchestration::parse_plan_detail_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective, snapshot.selected_option_id);
        } else {
            parsed = orchestration::parse_plan_final_text(std::string_view(result.text.data(), result.text.size()), snapshot);
        }
        if (parsed.ok) break;
        previous_error = parsed.error;
    }

    try {
        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->runtime->services().resolve<orchestration::PlanManager>()->mark_review_error(previous_error.empty() ? std::string("failed to parse revised plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
            co_return;
        }

        if (kind == RevisionKind::options) {
            entry->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_options(request_id,
                                                       std::move(parsed.draft.title),
                                                       std::move(parsed.draft.objective),
                                                       std::move(parsed.draft.options),
                                                       std::move(parsed.draft.selected_option_id));
        } else if (kind == RevisionKind::detail) {
            entry->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_detail(request_id,
                                                      std::move(parsed.draft.title),
                                                      std::move(parsed.draft.objective),
                                                      std::move(parsed.draft.items),
                                                      std::move(parsed.draft.global_risks),
                                                      std::move(parsed.draft.validation));
        } else {
            orchestration::PlanFinalDraft final_draft;
            final_draft.summary = std::move(parsed.draft.final_summary);
            final_draft.items = std::move(parsed.draft.final_items);
            final_draft.global_risks = std::move(parsed.draft.global_risks);
            final_draft.validation = std::move(parsed.draft.validation);
            final_draft.consistency_notes = std::move(parsed.draft.consistency_notes);
            entry->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_final(request_id, std::move(final_draft));
        }
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    } catch (const std::exception& e) {
        std::lock_guard state_lock(entry->state_mutex);
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
}

net::Task<void> WsSessionManager::handle_ws_plan_update_items(std::shared_ptr<WsHandler> ws,
                                                               std::string session_id,
                                                               std::vector<orchestration::PlanItem> items,
                                                               std::shared_ptr<SessionEntry> entry) {
    try {
        std::lock_guard state_lock(entry->state_mutex);
        entry->runtime->services().resolve<orchestration::PlanManager>()->apply_user_items(std::move(items));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
    }
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_plan_select_option(std::shared_ptr<WsHandler> ws,
                                                                std::string session_id,
                                                                std::string option_id,
                                                                int revision,
                                                                std::shared_ptr<SessionEntry> entry) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;
    std::string selected_option_id = option_id;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            request_id = entry->runtime->services().resolve<orchestration::PlanManager>()->begin_detailing(option_id, revision);
            snapshot = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();
            persist_plan_state(*entry);
            emit_plan_state(ws, snapshot);
        }

        std::string previous_error;
        std::string previous_output;
        orchestration::PlanParseResult parsed;
        auto& agent_loop = entry->runtime->services().resolve<net::IoContext>()->loop();
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto user_prompt = orchestration::build_plan_detail_prompt(snapshot, selected_option_id, previous_error, previous_output);
            auto result = co_await entry->runtime->services().resolve_ref<llm::ProviderClient>().chat_simple(agent_loop,
                "Return structured JSON only for the selected plan option detail.", user_prompt);
            previous_output = result.text;
            if (!result.ok()) {
                previous_error = result.error_message.empty() ? std::string("LLM request failed") : result.error_message;
                continue;
            }
            parsed = orchestration::parse_plan_detail_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective, selected_option_id);
            if (parsed.ok) break;
            previous_error = parsed.error;
        }

        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->runtime->services().resolve<orchestration::PlanManager>()->mark_failed(previous_error.empty() ? std::string("failed to parse detailed plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
            co_return;
        }
        entry->runtime->services().resolve<orchestration::PlanManager>()->apply_model_detail(selected_option_id, request_id,
                                               std::move(parsed.draft.title),
                                               std::move(parsed.draft.objective),
                                               std::move(parsed.draft.items),
                                               std::move(parsed.draft.global_risks),
                                               std::move(parsed.draft.validation));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_plan_apply_decision(std::shared_ptr<WsHandler> ws,
                                                                  std::string session_id,
                                                                  orchestration::PlanDecisionPatch patch,
                                                                  std::shared_ptr<SessionEntry> entry) {
    bool should_finalize = false;
    int finalize_revision = 0;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            should_finalize = entry->runtime->services().resolve<orchestration::PlanManager>()->apply_decision(patch);
            const auto& draft = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();
            finalize_revision = draft.revision;
            persist_plan_state(*entry);
            Json delta{{"event", "plan.apply_decision"},
                       {"session_id", draft.session_id},
                       {"workspace", draft.workspace},
                       {"revision", draft.revision},
                       {"item_id", patch.item_id},
                       {"decision_id", patch.decision_id},
                       {"selected_choice_id", patch.choice_id},
                       {"custom_note", patch.custom_note},
                       {"all_decisions_resolved", should_finalize}};
            emit_plan_delta(ws, draft, delta);
        }
        if (should_finalize) {
            co_await handle_ws_plan_finalize(ws, session_id, finalize_revision, entry);
        }
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_plan_finalize(std::shared_ptr<WsHandler> ws,
                                                            std::string session_id,
                                                            int revision,
                                                            std::shared_ptr<SessionEntry> entry) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            if (entry->runtime->services().resolve<orchestration::PlanManager>()->draft().stage == orchestration::PlanStage::final_review &&
                entry->runtime->services().resolve<orchestration::PlanManager>()->draft().finalized_input_revision == revision) {
                emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
                co_return;
            }
            request_id = entry->runtime->services().resolve<orchestration::PlanManager>()->begin_finalizing(revision);
            snapshot = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();
            persist_plan_state(*entry);
            emit_plan_state(ws, snapshot);
        }

        auto final_draft = build_local_final_draft(snapshot);
        std::lock_guard state_lock(entry->state_mutex);
        entry->runtime->services().resolve<orchestration::PlanManager>()->apply_model_final(request_id, std::move(final_draft));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_plan_confirm(std::shared_ptr<WsHandler> ws,
                                                          std::shared_ptr<EventBridge> event_sink,
                                                          std::string session_id,
                                                          int revision,
                                                          bool has_items,
                                                          std::vector<orchestration::PlanItem> items,
                                                          std::shared_ptr<SessionEntry> entry) {
    try {
        (void)has_items;
        (void)items;
        std::string execution_prompt;
        {
            std::lock_guard state_lock(entry->state_mutex);
            entry->runtime->services().resolve<orchestration::PlanManager>()->confirm(revision);
            auto confirmed = entry->runtime->services().resolve<orchestration::PlanManager>()->draft();

            entry->todo_manager.initialize_from_plan(confirmed);
            persist_todo_state(*entry);
            emit_todo_state(ws, entry->todo_manager.state());

            entry->runtime->services().resolve<orchestration::PlanManager>()->mark_executing();
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
            execution_prompt = build_execution_prompt(entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
        }

        co_await handle_ws_chat(ws, event_sink, session_id, std::move(execution_prompt), entry, false);
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
}

net::Task<void> WsSessionManager::handle_ws_plan_cancel(std::shared_ptr<WsHandler> ws,
                                                         std::string,
                                                         std::shared_ptr<SessionEntry> entry) {
    std::lock_guard state_lock(entry->state_mutex);
    entry->runtime->services().resolve<orchestration::PlanManager>()->cancel();
    persist_plan_state(*entry);
    emit_plan_state(ws, entry->runtime->services().resolve<orchestration::PlanManager>()->draft());
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_todo_update(std::shared_ptr<WsHandler> ws,
                                                         orchestration::TodoItem item,
                                                         std::shared_ptr<SessionEntry> entry) {
    orchestration::TodoDelta delta;
    std::string state_session_id;
    std::string state_workspace;
    {
        std::lock_guard state_lock(entry->state_mutex);
        delta = entry->todo_manager.upsert(std::move(item), std::string("manual"));
        persist_todo_state(*entry);
        state_session_id = entry->todo_manager.state().session_id;
        state_workspace = entry->todo_manager.state().workspace;
    }
    auto payload = orchestration::to_json_string(delta);
    auto msg = WsMessage::todo_delta(state_session_id, std::string(payload.data(), payload.size()));
    if (!state_workspace.empty()) msg.strings[std::string("workspace")] = state_workspace;
    queue_ws(ws, std::move(msg));
    co_return;
}

net::Task<void> WsSessionManager::handle_ws_chat(std::shared_ptr<WsHandler> ws,
                                                  std::shared_ptr<EventBridge> event_sink,
                                                  std::string session_id, std::string prompt,
                                                  std::shared_ptr<SessionEntry> entry,
                                                  bool persist_user_message) {
    log::info_fmt("WsSessionManager: chat session={}", session_id.c_str());

    struct ActiveRunGuard {
        std::shared_ptr<SessionEntry> entry;
        net::CancellationToken cancel;
        bool acquired = false;

        explicit ActiveRunGuard(std::shared_ptr<SessionEntry> e)
            : entry(std::move(e)), cancel() {
            std::lock_guard lock(entry->run_mutex);
            if (entry->active_run) return;
            entry->active_cancel = cancel;
            entry->active_run = true;
            acquired = true;
        }

        ~ActiveRunGuard() noexcept {
            if (!acquired) return;
            std::lock_guard lock(entry->run_mutex);
            entry->active_run = false;
            entry->active_cancel = net::CancellationToken();
        }
    } run_guard(entry);

    if (!run_guard.acquired) {
        log::warn_fmt("WsSessionManager: reject concurrent chat session={}", session_id.c_str());
        queue_ws(ws, WsMessage::error_msg(session_id, std::string("session is already running")));
        co_return;
    }

    // 自动更新会话名称：当 name 为空时，用用户提问内容截取前 40 字符作为名称
    // 用户手动重命名后 name 非空，不再自动更新
    if (persist_user_message && !prompt.empty()) {
        auto* history_db = entry->runtime->services().resolve<workspace::HistoryDB>();
        if (history_db) {
            auto sessions = history_db->list_sessions(
                entry->username,
                entry->session->workspace_context().workspace_name,
                config::SessionType::main);
            for (auto& s : sessions) {
                if (s.value("session_id", "") == session_id) {
                    auto existing_name = s.value("name", "");
                    if (existing_name.empty()) {
                        // 截取前 40 字符，去掉首尾空白
                        auto preview = prompt.substr(0, 40);
                        // 去掉换行
                        size_t nl = preview.find('\n');
                        if (nl != std::string::npos) preview = preview.substr(0, nl);
                        history_db->rename_session(session_id, preview);
                        log::debug_fmt("WsSessionManager: auto-rename session={} name={}", session_id, preview);
                    }
                    break;
                }
            }
        }
    }

    auto finalize_todos = [&](const llm::ChatResult& result) {
        if (entry->todo_manager.empty()) return;
        orchestration::TodoStatus status = orchestration::TodoStatus::failed;
        std::string summary = result.outcome.message.empty() ? std::string("execution failed") : result.outcome.message;
        if (result.outcome.ok()) {
            status = orchestration::TodoStatus::succeeded;
            summary = "execution completed";
        } else if (result.outcome.status == llm::RunStatus::cancelled) {
            status = orchestration::TodoStatus::cancelled;
            summary = "execution cancelled";
        } else if (result.outcome.status == llm::RunStatus::interrupted) {
            status = orchestration::TodoStatus::blocked;
            if (summary.empty()) summary = "execution interrupted";
        }
        if (result.outcome.ok()) {
            entry->todo_manager.mark_all_running_as(status, summary);
        } else {
            entry->todo_manager.mark_running_as(status, summary);
        }
        persist_todo_state(*entry);
        emit_todo_state(ws, entry->todo_manager.state());
    };
    auto send_terminal = [&](const llm::ChatResult& result) {
        finalize_todos(result);
        const auto outcome_json = llm::to_json(result.outcome);
        const auto usage_json = event_sink->response_usage_json();
        const auto latency = event_sink->response_latency();
        const double total_seconds = latency.total_seconds;
        const double ttfb_seconds = latency.has_ttfb ? latency.ttfb_seconds : 0.0;
        log::info_fmt("WsSessionManager: terminal session={} status={} ok={} usage_json={}",
                      session_id.c_str(), static_cast<int>(result.status), result.outcome.ok(), usage_json.c_str());
        if (!result.outcome.ok()) {
            auto message = result.error_message.empty() ? result.outcome.message : result.error_message;
            auto msg = WsMessage::error_msg(session_id, message, outcome_json);
            msg.strings[std::string("workspace")] = entry->session->workspace_context().workspace_name;
            auto error_json = msg.to_json();
            ws->loop().submit_task([ws, error_json = std::move(error_json)]() mutable {
                if (ws && ws->alive()) ws->queue_send(std::move(error_json));
            });
        }
        auto msg = WsMessage::done_with_outcome(session_id, usage_json, outcome_json, total_seconds, ttfb_seconds);
        msg.strings[std::string("workspace")] = entry->session->workspace_context().workspace_name;
        auto done_json = msg.to_json();
        ws->loop().submit_task([ws, done_json = std::move(done_json)]() mutable {
            if (ws && ws->alive()) ws->queue_send(std::move(done_json));
        });
    };

    try {
        // 用户消息持久化在 run_session_async 结束后统一处理（见下方遍历 history），
        // 这里不再提前持久化，避免重复写入
        auto& agent_loop = entry->runtime->services().resolve<net::IoContext>()->loop();
        auto msg_count_before = entry->session->history().messages().size();
        auto* event_bus = entry->runtime->services().resolve<base::EventBus>();
        if (event_bus) {
            event_sink->subscribe_to(*event_bus);
        }
        auto result = co_await entry->runtime->run_session_async({
            agent_loop, *entry->session, prompt, run_guard.cancel
        });

        auto& msgs = entry->session->history().messages();
        for (size_t i = msg_count_before; i < msgs.size(); ++i) {
            auto& m = msgs[i];
            auto role = m.role();
            if (role == acp::Role::Tool) {
                m.for_each_tool_result([&](const acp::ToolCallResult& r) {
                    entry->runtime->services().resolve<workspace::HistoryDB>()->append(
                        entry->session->session_id(),
                        std::string("tool"),
                        std::string(r.output.data(), r.output.size()),
                        std::string(r.tool_call_id.data(), r.tool_call_id.size()),
                        std::string(r.name.data(), r.name.size()));
                });
            } else if (role == acp::Role::Assistant) {
                auto text = m.get_all_text();
                auto calls = m.get_tool_calls();
                std::vector<acp::ToolCallRequest> std_calls;
                for (auto& c : calls) std_calls.push_back(std::move(c));
                entry->session->persist_assistant_message(text, std_calls, *entry->runtime->services().resolve<workspace::HistoryDB>());
            } else if (role == acp::Role::User) {
                auto text = m.get_all_text();
                entry->runtime->services().resolve<workspace::HistoryDB>()->append(
                    entry->session->session_id(),
                    std::string("user"), text);
            }
        }
        entry->runtime->services().resolve<workspace::HistoryDB>()->flush();

        log::info_fmt("WsSessionManager: chat done session={} status={} outcome={}",
                      session_id.c_str(), static_cast<int>(result.status),
                      llm::to_string(result.outcome.reason));
        send_terminal(result);
    } catch (const net::OperationCancelled& e) {
        log::warn_fmt("WsSessionManager: chat cancelled: {}", e.what());
        auto result = llm::ChatResult::cancelled(std::string(e.what()));
        send_terminal(result);
    } catch (const std::exception& e) {
        log::error_fmt("WsSessionManager: chat error: {}", e.what());
        auto result = llm::ChatResult::internal_error(std::string(e.what()));
        send_terminal(result);
    }
}

} // namespace ben_gear::server
