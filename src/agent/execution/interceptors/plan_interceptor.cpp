#include "agent/execution/interceptors/plan_interceptor.hpp"

#include <string_view>

namespace ben_gear::agent::execution {

// ─── 只读工具白名单 ──────────────────────────────────────────────
//
// 计划审核期间（drafting / reviewing）只允许这些工具通过，
// 其他所有工具调用被拦截并返回 blocked 结果。

static constexpr std::string_view READ_ONLY_TOOLS[] = {
    "read_file",
    "search_files",
    "grep_content",
    "list_files",
    "read_memory",
    "read_soul",
    "read_rules",
    "read_user",
    "list_sessions",
    "list_workspaces",
    "get_skill",
    "list_skills",
    "list_tools",
    "mcp_list_tools",
    "search_history",
    "show_plan",
    "list_plans",
    "plan_status",
};

bool PlanInterceptor::is_read_only_tool(const std::string& name) const {
    for (auto allowed : READ_ONLY_TOOLS) {
        if (name == allowed) return true;
    }
    return false;
}

void PlanInterceptor::before_tools(
    std::vector<capabilities::tool::ToolCallRequest>& calls,
    std::vector<capabilities::tool::ToolCallResult>& blocked,
    const llm::ConversationHistory& /*history*/,
    LoopSnapshot& /*ctx*/) {

    if (!plan_mgr_ || !plan_mgr_->read_only_tools()) return;

    // 分类：只读通过，其他拦截
    std::vector<capabilities::tool::ToolCallRequest> allowed;
    for (auto& call : calls) {
        std::string name(call.name.data(), call.name.size());
        if (is_read_only_tool(name)) {
            allowed.push_back(std::move(call));
        } else {
            capabilities::tool::ToolCallResult blocked_result;
            blocked_result.tool_call_id = std::string(call.id.data(), call.id.size());
            blocked_result.name = std::move(name);
            blocked_result.output =
                "工具被拦截：当前处于计划审核模式，不允许执行写操作。"
                "请先确认计划（/approve）后再调用此工具。";
            blocked.push_back(std::move(blocked_result));
        }
    }
    calls = std::move(allowed);
}

std::string PlanInterceptor::should_stop(
    const LoopSnapshot& /*snapshot*/,
    const llm::ConversationHistory& /*history*/) {

    if (!plan_mgr_) return {};

    auto status = plan_mgr_->status();
    if (status == orchestration::PlanStatus::cancelled) {
        return "计划已取消";
    }
    if (status == orchestration::PlanStatus::failed) {
        return "计划执行失败: " + plan_mgr_->draft().error;
    }
    return {};
}

}  // namespace ben_gear::agent::execution
