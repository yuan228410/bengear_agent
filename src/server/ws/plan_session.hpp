#pragma once

#include "config/settings.hpp"
#include "net/task.hpp"
#include "orchestration/plan.hpp"
#include "orchestration/plan_parser.hpp"
#include "orchestration/serializer.hpp"
#include "orchestration/todo.hpp"
#include "server/session/pool.hpp"
#include "workspace/resolver.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ben_gear {

namespace server {

class WsHandler;
class EventBridge;
class WsSessionManager;
struct WsMessage;

struct PlanChatRequest {
    std::string mode;
    int revision = 0;
    std::string note;
    std::string custom_idea;
    std::string item_id;
    std::string decision_id;
};

/// 计划会话状态机 — 封装计划流程的异步 I/O + 状态读写
///
/// 职责：
/// - 在锁外执行 LLM 调用（避免持锁期间协程挂起阻塞其他请求）
/// - 在锁内读写 SessionEntry 状态
/// - 向 WebSocket 推送状态更新
///
/// 使用三阶段模式：
///   1. write_state()   — 锁内写入初始状态，释放锁
///   2. async_execute() — 锁外异步执行（LLM 调用）
///   3. write_result()  — 锁内写入结果，释放锁
class PlanSession {
public:
    PlanSession(std::shared_ptr<WsHandler> ws,
                std::shared_ptr<EventBridge> event_bridge,
                std::shared_ptr<SessionEntry> entry);

    // ─── 计划流程方法 ────────────────────────────────────────────

    /// plan_start — 生成选项阶段
    net::Task<void> start(std::string session_id, std::string prompt, std::string note);

    /// plan_chat — 修订选项/细节/最终方案
    net::Task<void> chat(std::string session_id, PlanChatRequest request);

    /// plan_select_option — 选择方案并生成细节
    net::Task<void> select_option(std::string session_id, std::string option_id, int revision);

    /// plan_apply_decision — 应用用户决策（同步，无 I/O）
    void apply_decision(std::string session_id, orchestration::PlanDecisionPatch patch);

    /// plan_finalize — 定稿最终方案
    net::Task<void> finalize(std::string session_id, int revision);

    /// plan_confirm — 确认并开始执行
    net::Task<void> confirm(std::string session_id, int revision, bool has_items,
                            std::vector<orchestration::PlanItem> items);

    /// plan_cancel — 取消计划（同步，无 I/O）
    void cancel(std::string session_id);

    /// plan_update_items — 手动更新计划条目（同步，无 I/O）
    void update_items(std::string session_id, std::vector<orchestration::PlanItem> items);

    /// todo_update — 手动更新 TODO
    net::Task<void> todo_update(std::string session_id, orchestration::TodoItem item);

    // ─── 公开状态写方法（供 WsSessionManager 调用）───────────────

    void write_start_state(const std::string& session_id, const std::string& prompt, const std::string& note);
    void write_start_result(const std::string& session_id, const orchestration::PlanParseResult& parsed);
    void write_confirm_state(const std::string& session_id, int revision, std::string& out_execution_prompt);

    enum class RevisionKind { options, detail, final };

private:
    // ─── 私有状态读写（锁内）─────────────────────────────────────
    void write_chat_result(const std::string& session_id, uint64_t request_id,
                           const orchestration::PlanParseResult& parsed,
                           RevisionKind kind, const PlanChatRequest& request);
    void write_select_option_result(const std::string& session_id, std::string& option_id,
                                    uint64_t request_id, const orchestration::PlanParseResult& parsed);
    void write_finalize_result(const std::string& session_id, uint64_t request_id,
                               const orchestration::PlanFinalDraft& final_draft);

    // ─── 异步 LLM 调用（锁外）────────────────────────────────────
    net::Task<orchestration::PlanParseResult> call_llm_for_options(
        const std::string& session_id, const std::string& prompt, const std::string& note,
        const std::string& workspace);
    net::Task<orchestration::PlanParseResult> call_llm_for_chat(
        const std::string& session_id, const std::string& workspace,
        const std::string& objective, const std::string& selected_option_id,
        RevisionKind kind, const std::string& item_id, const std::string& decision_id,
        const std::string& feedback);
    net::Task<orchestration::PlanParseResult> call_llm_for_detail(
        const std::string& session_id, const orchestration::PlanDraft& snapshot,
        const std::string& workspace, const std::string& objective,
        const std::string& selected_option_id,
        const std::string& previous_error, const std::string& previous_output);
    orchestration::PlanFinalDraft build_final_draft(const orchestration::PlanDraft& draft);

    // ─── 辅助 ────────────────────────────────────────────────────
    void queue_ws(WsMessage msg);

    std::shared_ptr<WsHandler> ws_;
    std::shared_ptr<EventBridge> event_bridge_;
    std::shared_ptr<SessionEntry> entry_;
};

} // namespace server
} // namespace ben_gear