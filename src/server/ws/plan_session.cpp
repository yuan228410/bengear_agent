#include "server/ws/plan_session.hpp"
#include "server/ws/handler.hpp"
#include "server/core/event_bridge.hpp"
#include "server/ws/session_message_dispatcher.hpp"
#include "server/ws/protocol.hpp"
#include "workspace/resolver.hpp"
#include "workspace/types.hpp"
#include "log/logger.hpp"
#include "net/event_loop.hpp"
#include "orchestration/plan_parser.hpp"
#include "domain/errors.hpp"
#include "llm/provider_client.hpp"

namespace ben_gear::server {

PlanSession::PlanSession(std::shared_ptr<WsHandler> ws,
                         std::shared_ptr<EventBridge> event_bridge,
                         std::shared_ptr<SessionEntry> entry)
    : ws_(std::move(ws)),
      event_bridge_(std::move(event_bridge)),
      entry_(std::move(entry)) {}

// ─── 辅助 ────────────────────────────────────────────────────────

void PlanSession::queue_ws(WsMessage msg) {
    if (!ws_ || !ws_->alive()) return;
    auto json = msg.to_json();
    auto& loop = ws_->loop();
    if (loop.is_loop_thread()) {
        ws_->queue_send(std::move(json));
    } else {
        auto handler = ws_;
        loop.submit_task([handler, json = std::move(json)]() mutable {
            if (handler && handler->alive()) handler->queue_send(std::move(json));
        });
    }
}

orchestration::PlanFinalDraft PlanSession::build_final_draft(const orchestration::PlanDraft& draft) {
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

// ─── plan_start ──────────────────────────────────────────────────

void PlanSession::write_start_state(const std::string& session_id, const std::string& prompt, const std::string& note) {
    std::lock_guard lock(entry_->state_mutex);
    orchestration::PlanCommand command;
    command.session_id = session_id;
    command.workspace = entry_->session->workspace_context().workspace_name;
    command.prompt = prompt;
    command.note = note;
    entry_->runtime->services().resolve<orchestration::PlanManager>()->start(command);
    entry_->session->persist_message(std::string("user"), prompt, *entry_->runtime->services().resolve<workspace::HistoryDB>());
    entry_->session->persist_message(std::string("plan_anchor"), std::string(), *entry_->runtime->services().resolve<workspace::HistoryDB>());
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

void PlanSession::write_start_result(const std::string& session_id,
                                     const orchestration::PlanParseResult& parsed) {
    (void)session_id;
    std::lock_guard lock(entry_->state_mutex);
    if (!parsed.ok) {
        entry_->runtime->services().resolve<orchestration::PlanManager>()->mark_failed(
            parsed.error.empty() ? std::string("failed to parse plan after retries") : parsed.error);
        persist_plan_state(*entry_);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
        return;
    }
    auto draft_copy = parsed.draft;
    draft_copy.plan_id = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().plan_id;
    entry_->runtime->services().resolve<orchestration::PlanManager>()->restore(std::move(draft_copy));
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

net::Task<orchestration::PlanParseResult> PlanSession::call_llm_for_options(
        const std::string& session_id, const std::string& prompt, const std::string& note,
        const std::string& workspace) {
    orchestration::PlanParseResult parsed;
    std::string previous_error;
    std::string previous_output;
    auto& loop = entry_->runtime->services().resolve<net::IoContext>()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto user_prompt = orchestration::build_plan_options_prompt(prompt, note, previous_error, previous_output);
        llm::ChatRequest request;
        request.system_prompt = "Return structured JSON only for the web plan option review state.";
        request.user_prompt = user_prompt;
        auto result = co_await entry_->runtime->services().resolve<llm::ProviderClient>()->chat_async(loop, request);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? std::string("LLM request failed") : result.error_message;
            continue;
        }
        parsed = orchestration::parse_plan_options_text(
            std::string_view(result.text.data(), result.text.size()), session_id, workspace, prompt);
        if (parsed.ok) break;
        previous_error = parsed.error;
    }
    co_return parsed;
}

net::Task<void> PlanSession::start(std::string session_id, std::string prompt, std::string note) {
    if (prompt.empty()) {
        queue_ws(WsMessage::error_msg(session_id, std::string("plan prompt is empty")));
        co_return;
    }
    auto workspace = entry_->session->workspace_context().workspace_name;

    // 阶段一：锁内写入初始状态，释放锁
    write_start_state(session_id, prompt, note);

    // 阶段二：锁外异步执行 LLM 调用
    auto parsed = co_await call_llm_for_options(session_id, prompt, note, workspace);

    // 阶段三：锁内写入结果
    write_start_result(session_id, parsed);
}

// ─── plan_chat ───────────────────────────────────────────────────

net::Task<orchestration::PlanParseResult> PlanSession::call_llm_for_chat(
        const std::string& session_id, const std::string& workspace,
        const std::string& objective, const std::string& selected_option_id,
        RevisionKind kind, const std::string& item_id, const std::string& decision_id,
        const std::string& feedback) {
    orchestration::PlanParseResult parsed;
    std::string previous_error;
    std::string previous_output;
    auto& loop = entry_->runtime->services().resolve<net::IoContext>()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string user_prompt;
        if (kind == RevisionKind::options) {
            user_prompt = orchestration::build_plan_options_revision_prompt(
                orchestration::PlanDraft(), feedback, previous_error, previous_output);
        } else if (kind == RevisionKind::detail) {
            user_prompt = orchestration::build_plan_decision_revision_prompt(
                orchestration::PlanDraft(), item_id, decision_id, feedback, previous_error, previous_output);
        } else {
            user_prompt = orchestration::build_plan_final_revision_prompt(
                orchestration::PlanDraft(), feedback, previous_error, previous_output);
        }
        llm::ChatRequest llm_request;
        llm_request.system_prompt = "Revise the structured plan and return JSON only.";
        llm_request.user_prompt = user_prompt;
        auto result = co_await entry_->runtime->services().resolve<llm::ProviderClient>()->chat_async(loop, llm_request);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? std::string("LLM request failed") : result.error_message;
            continue;
        }
        if (kind == RevisionKind::options) {
            parsed = orchestration::parse_plan_options_text(
                std::string_view(result.text.data(), result.text.size()), session_id, workspace, objective);
        } else if (kind == RevisionKind::detail) {
            parsed = orchestration::parse_plan_detail_text(
                std::string_view(result.text.data(), result.text.size()), session_id, workspace, objective, selected_option_id);
        } else {
            parsed = orchestration::parse_plan_final_text(
                std::string_view(result.text.data(), result.text.size()), orchestration::PlanDraft());
        }
        if (parsed.ok) break;
        previous_error = parsed.error;
    }
    co_return parsed;
}

void PlanSession::write_chat_result(const std::string& session_id, uint64_t request_id,
                                    const orchestration::PlanParseResult& parsed,
                                    RevisionKind kind, const PlanChatRequest& request) {
    (void)session_id;
    (void)request;
    std::lock_guard lock(entry_->state_mutex);
    if (!parsed.ok) {
        entry_->runtime->services().resolve<orchestration::PlanManager>()->mark_review_error(
            parsed.error.empty() ? std::string("failed to parse revised plan after retries") : parsed.error);
        persist_plan_state(*entry_);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
        return;
    }
    if (kind == RevisionKind::options) {
        entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_options(
            request_id, std::move(parsed.draft.title), std::move(parsed.draft.objective),
            std::move(parsed.draft.options), std::move(parsed.draft.selected_option_id));
    } else if (kind == RevisionKind::detail) {
        entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_detail(
            request_id, std::move(parsed.draft.title), std::move(parsed.draft.objective),
            std::move(parsed.draft.items), std::move(parsed.draft.global_risks),
            std::move(parsed.draft.validation));
    } else {
        orchestration::PlanFinalDraft final_draft;
        final_draft.summary = std::move(parsed.draft.final_summary);
        final_draft.items = std::move(parsed.draft.final_items);
        final_draft.global_risks = std::move(parsed.draft.global_risks);
        final_draft.validation = std::move(parsed.draft.validation);
        final_draft.consistency_notes = std::move(parsed.draft.consistency_notes);
        entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_revised_final(
            request_id, std::move(final_draft));
    }
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

net::Task<void> PlanSession::chat(std::string session_id, PlanChatRequest request) {
    auto feedback = request.custom_idea.empty() ? request.note : request.custom_idea;
    if (feedback.empty()) {
        queue_ws(WsMessage::error_msg(session_id, std::string("plan revision feedback is empty")));
        co_return;
    }

    uint64_t request_id = 0;
    RevisionKind kind = RevisionKind::options;
    orchestration::PlanDraft snapshot;
    auto workspace = entry_->session->workspace_context().workspace_name;
    auto objective = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().objective;
    auto selected_option_id = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().selected_option_id;

    // 阶段一：锁内验证状态并写入初始状态
    try {
        std::lock_guard lock(entry_->state_mutex);
        const auto& current = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
        if (request.mode == "reject_options") {
            kind = RevisionKind::options;
        } else if (request.mode == "reject_decision") {
            kind = RevisionKind::detail;
        } else if (request.mode == "revise_final") {
            kind = RevisionKind::final;
        } else {
            kind = current.stage == orchestration::PlanStage::option_review ? RevisionKind::options
                    : current.stage == orchestration::PlanStage::decision_review ? RevisionKind::detail
                    : RevisionKind::final;
        }
        request_id = entry_->runtime->services().resolve<orchestration::PlanManager>()->begin_chat_revision(request.revision);
        snapshot = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
        persist_plan_state(*entry_);
        emit_plan_state(ws_, snapshot);
    } catch (const std::exception& e) {
        queue_ws(WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard lock(entry_->state_mutex);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
        co_return;
    }

    // 阶段二：锁外 LLM 调用
    auto parsed = co_await call_llm_for_chat(
        session_id, workspace, objective, selected_option_id,
        kind, request.item_id, request.decision_id, feedback);

    // 阶段三：锁内写入结果
    write_chat_result(session_id, request_id, parsed, kind, request);
}

// ─── plan_update_items ────────────────────────────────────────────

void PlanSession::update_items(std::string session_id, std::vector<orchestration::PlanItem> items) {
    try {
        std::lock_guard lock(entry_->state_mutex);
        entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_user_items(std::move(items));
        persist_plan_state(*entry_);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
    } catch (const std::exception& e) {
        queue_ws(WsMessage::error_msg(std::move(session_id), std::string(e.what())));
    }
}

// ─── plan_select_option ───────────────────────────────────────────

net::Task<orchestration::PlanParseResult> PlanSession::call_llm_for_detail(
        const std::string& session_id, const orchestration::PlanDraft& snapshot,
        const std::string& workspace, const std::string& objective,
        const std::string& selected_option_id,
        const std::string& previous_error, const std::string& previous_output) {
    orchestration::PlanParseResult parsed;
    auto& loop = entry_->runtime->services().resolve<net::IoContext>()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto user_prompt = orchestration::build_plan_detail_prompt(
            snapshot, selected_option_id, previous_error, previous_output);
        llm::ChatRequest request;
        request.system_prompt = "Return structured JSON only for the selected plan option detail.";
        request.user_prompt = user_prompt;
        auto result = co_await entry_->runtime->services().resolve<llm::ProviderClient>()->chat_async(loop, request);
        if (!result.ok()) continue;
        parsed = orchestration::parse_plan_detail_text(
            std::string_view(result.text.data(), result.text.size()),
            session_id, workspace, objective, selected_option_id);
        if (parsed.ok) break;
    }
    co_return parsed;
}

void PlanSession::write_select_option_result(const std::string& session_id, std::string& option_id,
                                             uint64_t request_id, const orchestration::PlanParseResult& parsed) {
    (void)session_id;
    std::lock_guard lock(entry_->state_mutex);
    if (!parsed.ok) {
        entry_->runtime->services().resolve<orchestration::PlanManager>()->mark_failed(
            parsed.error.empty() ? std::string("failed to parse detailed plan after retries") : parsed.error);
        persist_plan_state(*entry_);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
        return;
    }
    entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_model_detail(
        option_id, request_id,
        std::move(parsed.draft.title), std::move(parsed.draft.objective),
        std::move(parsed.draft.items), std::move(parsed.draft.global_risks),
        std::move(parsed.draft.validation));
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

net::Task<void> PlanSession::select_option(std::string session_id, std::string option_id, int revision) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;
    auto workspace = entry_->session->workspace_context().workspace_name;
    auto objective = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().objective;

    // 阶段一：锁内写入初始状态
    {
        std::lock_guard lock(entry_->state_mutex);
        request_id = entry_->runtime->services().resolve<orchestration::PlanManager>()->begin_detailing(option_id, revision);
        snapshot = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
        persist_plan_state(*entry_);
        emit_plan_state(ws_, snapshot);
    }

    // 阶段二：锁外 LLM 调用（复用 call_llm_for_detail）
    auto parsed = co_await call_llm_for_detail(session_id, snapshot, workspace, objective, option_id,
                                                std::string{}, std::string{});

    // 阶段三：锁内写入结果
    write_select_option_result(session_id, option_id, request_id, parsed);
}

// ─── plan_apply_decision ──────────────────────────────────────────

void PlanSession::apply_decision(std::string session_id, orchestration::PlanDecisionPatch patch) {
    bool should_finalize = false;
    int finalize_revision = 0;
    orchestration::PlanDraft draft;
    {
        std::lock_guard lock(entry_->state_mutex);
        should_finalize = entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_decision(patch);
        draft = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
        finalize_revision = draft.revision;
        persist_plan_state(*entry_);
        Json delta{{"event", "plan.apply_decision"},
                   {"session_id", draft.session_id},
                   {"workspace", draft.workspace},
                   {"revision", draft.revision},
                   {"item_id", patch.item_id},
                   {"decision_id", patch.decision_id},
                   {"selected_choice_id", patch.choice_id},
                   {"custom_note", patch.custom_note},
                   {"all_decisions_resolved", should_finalize}};
        emit_plan_delta(ws_, draft, delta);
    }
    // finalize 在锁外执行
    if (should_finalize) {
        finalize(std::move(session_id), finalize_revision);
    }
}

// ─── plan_finalize ────────────────────────────────────────────────

void PlanSession::write_finalize_result(const std::string& session_id, uint64_t request_id,
                                        const orchestration::PlanFinalDraft& final_draft) {
    (void)session_id;
    std::lock_guard lock(entry_->state_mutex);
    entry_->runtime->services().resolve<orchestration::PlanManager>()->apply_model_final(
        request_id, final_draft);
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

net::Task<void> PlanSession::finalize(std::string session_id, int revision) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;

    // 阶段一：锁内检查并初始化
    {
        std::lock_guard lock(entry_->state_mutex);
        if (entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().stage == orchestration::PlanStage::final_review &&
            entry_->runtime->services().resolve<orchestration::PlanManager>()->draft().finalized_input_revision == revision) {
            emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
            co_return;
        }
        request_id = entry_->runtime->services().resolve<orchestration::PlanManager>()->begin_finalizing(revision);
        snapshot = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
        persist_plan_state(*entry_);
        emit_plan_state(ws_, snapshot);
    }

    // 阶段二：锁外构建最终草稿（纯计算，无 I/O）
    auto final_draft = build_final_draft(snapshot);

    // 阶段三：锁内写入结果
    write_finalize_result(session_id, request_id, final_draft);
}

// ─── plan_confirm ─────────────────────────────────────────────────

void PlanSession::write_confirm_state(const std::string& session_id, int revision,
                                      std::string& out_execution_prompt) {
    (void)session_id;
    std::lock_guard lock(entry_->state_mutex);
    entry_->runtime->services().resolve<orchestration::PlanManager>()->confirm(revision);
    auto confirmed = entry_->runtime->services().resolve<orchestration::PlanManager>()->draft();
    entry_->todo_manager.initialize_from_plan(confirmed);
    persist_todo_state(*entry_);
    emit_todo_state(ws_, entry_->todo_manager.state());
    entry_->runtime->services().resolve<orchestration::PlanManager>()->mark_executing();
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
    out_execution_prompt = build_execution_prompt(confirmed);
}

net::Task<void> PlanSession::confirm(std::string session_id, int revision, bool has_items,
                                     std::vector<orchestration::PlanItem> items) {
    (void)has_items;
    (void)items;
    try {
        // 阶段一：锁内确认
        std::string execution_prompt;
        write_confirm_state(session_id, revision, execution_prompt);

        // 阶段二：启动执行
        // 注意：实际执行由 WsSessionManager::handle_ws_chat 处理
        // 此处仅记录日志
        log::info_fmt("PlanSession: confirm session_id={} prompt_len={}",
                      session_id, execution_prompt.size());
    } catch (const std::exception& e) {
        queue_ws(WsMessage::error_msg(session_id, std::string(e.what())));
        std::lock_guard lock(entry_->state_mutex);
        emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
    }
    co_return;
}

// ─── plan_cancel ──────────────────────────────────────────────────

void PlanSession::cancel(std::string session_id) {
    (void)session_id;
    std::lock_guard lock(entry_->state_mutex);
    entry_->runtime->services().resolve<orchestration::PlanManager>()->cancel();
    persist_plan_state(*entry_);
    emit_plan_state(ws_, entry_->runtime->services().resolve<orchestration::PlanManager>()->draft());
}

// ─── todo_update ──────────────────────────────────────────────────

net::Task<void> PlanSession::todo_update(std::string session_id, orchestration::TodoItem item) {
    orchestration::TodoDelta delta;
    std::string state_session_id;
    std::string state_workspace;
    {
        std::lock_guard lock(entry_->state_mutex);
        delta = entry_->todo_manager.upsert(std::move(item), std::string("manual"));
        persist_todo_state(*entry_);
        state_session_id = entry_->todo_manager.state().session_id;
        state_workspace = entry_->todo_manager.state().workspace;
    }
    auto payload = orchestration::to_json_string(delta);
    auto msg = WsMessage::todo_delta(state_session_id, std::string(payload.data(), payload.size()));
    if (!state_workspace.empty()) msg.strings[std::string("workspace")] = state_workspace;
    queue_ws(std::move(msg));
    co_return;
}

} // namespace ben_gear::server