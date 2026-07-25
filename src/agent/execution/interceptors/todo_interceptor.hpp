#pragma once

#include "agent/execution/interceptor.hpp"
#include "orchestration/todo.hpp"

#include <string>

namespace ben_gear::agent::execution {

/// TODO 拦截器 — 负责注入 TODO 上下文并校验完成状态
///
/// 通用组件，不依赖计划模式/非计划模式：
/// - before_llm：如果 TodoManager 非空，将当前 TODO 状态注入到 LLM 上下文中
/// - after_tools：工具执行后检查所有 TODO 是否已终结
///   - 全部完成 → reset TodoManager
///   - 仍有 pending → 回推 pending 摘要到 history
class TodoInterceptor : public IInterceptor {
public:
    explicit TodoInterceptor(orchestration::TodoManager* todo_manager,
                             base::EventBus* event_bus)
        : todo_mgr_(todo_manager), event_bus_(event_bus) {}

    const char* name() const noexcept override { return "Todo"; }

    void before_llm(llm::ConversationHistory& history,
                    LoopSnapshot& /*snapshot*/) override;

    void after_tools(const std::vector<acp::ToolCallResult>& /*results*/,
                     llm::ConversationHistory& history,
                     LoopSnapshot& /*snapshot*/) override;

private:
    /// 生成 TODO 状态摘要文本（纯文本，供 LLM 理解）
    static std::string build_summary(const orchestration::TodoState& state);

    orchestration::TodoManager* todo_mgr_;
    base::EventBus* event_bus_;
};

} // namespace ben_gear::agent::execution
