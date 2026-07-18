#pragma once

#include "agent/execution/interceptor.hpp"
#include "orchestration/plan.hpp"

#include <string>
#include <unordered_set>

namespace ben_gear::agent::execution {

/// 计划模式拦截器
///
/// 在计划审核（drafting / reviewing）阶段拦截所有工具的写操作，
/// 确保 LLM 不会在计划未确认时执行副作用操作。
/// 计划进入终态（cancelled / failed）时通过 should_stop 终止循环。
class PlanInterceptor : public IInterceptor {
public:
    explicit PlanInterceptor(orchestration::PlanManager* plan_manager)
        : plan_mgr_(plan_manager) {}

    const char* name() const noexcept override { return "Plan"; }

    /// 计划审核期间：只允许只读工具，其他全部拦截
    void before_tools(std::vector<capabilities::tool::ToolCallRequest>& calls,
                      std::vector<capabilities::tool::ToolCallResult>& blocked,
                      const llm::ConversationHistory& /*history*/,
                      LoopSnapshot& /*ctx*/) override;

    /// 计划终止时返回停止原因
    std::string should_stop(const LoopSnapshot& snapshot,
                            const llm::ConversationHistory& history) override;

private:
    /// 只读工具白名单：计划审核期间允许调用的工具
    bool is_read_only_tool(const std::string& name) const;

    orchestration::PlanManager* plan_mgr_;
};

}  // namespace ben_gear::agent::execution
