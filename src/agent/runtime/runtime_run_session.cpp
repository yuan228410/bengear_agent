#include "agent/runtime/runtime.hpp"
#include "agent/execution/loop.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/core/event_sink.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::agent::runtime {

net::Task<llm::ChatResult> Runtime::run_session_async(SessionRunConfig config) {
    return run_session_async(config.loop, config.session, std::move(config.prompt),
                             config.event_sink, config.cancel, config.tool_override);
}

net::Task<llm::ChatResult> Runtime::run_session_async(
    net::EventLoop& loop, workspace::Session& session,
    std::string prompt, const AgentEventSinks& event_sink,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry* tool_override) {

    const capabilities::tool::ToolRegistry& tool_reg =
        tool_override ? *tool_override : tools_.registry_;
    auto& history = session.history();

    // 构建系统提示 — 包含 SOUL/RULES/USER/MEMORY/skills
    auto sys_prompt = memory_.builder_->build();

    // 计划模式：通过提示词约束行为，不拦截工具
    if (orch_.plans_.is_active()) {
        if (orch_.plans_.is_reviewing()) {
            sys_prompt += "\n\n## Plan Mode — Reviewing\n"
                "You are in the planning phase. Your ONLY job is to understand the request, "
                "gather context, explore the codebase, and produce a clear plan.\n\n"
                "Rules:\n"
                "- DO NOT implement, modify, or execute anything beyond information gathering.\n"
                "- DO NOT write production code, apply fixes, run migrations, or make changes.\n"
                "- You MAY read files, search code, ask questions, and write the plan to PLAN.md.\n"
                "- Keep the plan actionable and concise — what, why, how, risks, order of steps.\n"
                "- The user will review and `/approve` when ready. Do not proceed until approved.";
        } else if (orch_.plans_.is_executing()) {
            sys_prompt += "\n\n## Plan Mode — Executing\n"
                "You are executing the approved plan. Follow each item in order.\n"
                "- Work through the plan step by step. Report progress after each.\n"
                "- If you encounter blockers, pause and ask before deviating from the plan.";
        }
    }
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    // 创建执行循环（不注入工具过滤拦截器 — 由提示词约束）
    execution::LoopConfig loop_config;
    loop_config.max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    loop_config.max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    loop_config.max_parallel_tools = max_parallel_tools_;

    execution::ExecutionLoop exec_loop(
        loop_config, provider_, tool_reg, infra_.core_pool, settings_);

    co_return co_await exec_loop.run(loop, session, history, event_sink, cancel);
}

} // namespace ben_gear::agent::runtime
