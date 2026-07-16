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

    // 根据当前状态配置提示词模式
    if (orch_.plans_.is_reviewing()) {
        memory_.builder_->set_mode(memory::PromptMode::plan_reviewing);
    } else if (orch_.plans_.is_executing()) {
        memory_.builder_->set_mode(memory::PromptMode::plan_executing);
    } else {
        memory_.builder_->set_mode(memory::PromptMode::normal);
    }
    auto sys_prompt = memory_.builder_->build();
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
