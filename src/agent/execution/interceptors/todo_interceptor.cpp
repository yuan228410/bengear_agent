#include "agent/execution/interceptors/todo_interceptor.hpp"
#include "agent/core/events.hpp"
#include "orchestration/serializer.hpp"
#include "log/logger.hpp"

namespace ben_gear::agent::execution {

void TodoInterceptor::before_llm(llm::ConversationHistory& history,
                                 LoopSnapshot& /*snapshot*/) {
    if (!todo_mgr_ || todo_mgr_->empty()) return;

    // 注入当前 TODO 状态作为 system 上下文，LLM 自行理解
    auto summary = build_summary(todo_mgr_->state());
    history.add_system(std::string_view(summary.data(), summary.size()));
}

void TodoInterceptor::after_tools(const std::vector<acp::ToolCallResult>& /*results*/,
                                  llm::ConversationHistory& history,
                                  LoopSnapshot& /*snapshot*/) {
    if (!todo_mgr_ || todo_mgr_->empty()) return;

    if (todo_mgr_->all_completed()) {
        // 全部终结 → 记录日志、重置、通知前端
        log::info_fmt("TodoInterceptor: all {} items completed, resetting",
                      todo_mgr_->state().items.size());

        // 保存最后状态用于通知
        auto final_state_json = orchestration::to_json_string(todo_mgr_->state());
        todo_mgr_->reset();

        // 通知前端清空
        if (event_bus_) {
            event_bus_->publish(agent::TodoUpdateEvent{
                {}, {}, {}, {}, {}, std::string("clear"), 0, {}});
        }

        // 回推终结通知到 history，LLM 下一轮看到后可继续后续任务
        history.add_system(std::string_view("All TODO items have been completed. Proceed to the next task."));
    } else if (todo_mgr_->has_pending()) {
        // 还有未完成项 → 回推 pending 摘要
        auto summary = build_summary(todo_mgr_->state());
        history.add_system(std::string_view(summary.data(), summary.size()));
    }
    // 全部 blocked/skipped/failed → 不回推，让 LLM 自己决定
}

// static
std::string TodoInterceptor::build_summary(const orchestration::TodoState& state) {
    // 纯文本摘要，不包含任何 UI 标记
    std::string out("Current TODO state:\n");
    for (const auto& item : state.items) {
        out += "- [" + std::string(orchestration::to_string(item.status)) + "] ";
        out += item.title;
        out += " (" + std::to_string(item.progress) + "%)";
        if (!item.result_summary.empty()) {
            out += " " + item.result_summary;
        }
        out += "\n";
    }
    out += "Before starting a step, set its status to 'running'. "
           "After completing it, set its status to 'succeeded' and start the next step.";
    return out;
}

} // namespace ben_gear::agent::execution
