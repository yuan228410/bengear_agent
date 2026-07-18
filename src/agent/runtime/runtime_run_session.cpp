#include "agent/runtime/runtime.hpp"
#include "agent/execution/loop.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/execution/service_interface.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/execution/interceptors/plan_interceptor.hpp"
#include "agent/execution/interceptors/compaction_interceptor.hpp"
#include "agent/core/event_sink.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::agent::runtime {

/// IExecutionLoopServices 默认实现 — 桥接 Runtime 的 provider/tools
struct RuntimeLoopServices : execution::IExecutionLoopServices {
    llm::ProviderClient& provider;
    const capabilities::tool::ToolRegistry& tools;

    RuntimeLoopServices(llm::ProviderClient& provider_ref,
                        const capabilities::tool::ToolRegistry& tools_ref)
        : provider(provider_ref), tools(tools_ref) {}

    net::Task<llm::StreamResult> chat_stream(
        net::EventLoop& loop,
        const llm::ConversationHistory& history,
        const capabilities::tool::ToolRegistry& tool_reg,
        const capabilities::tool::ToolChoiceConfig& tool_choice,
        llm::StreamHandlers handlers,
        const net::CancellationToken& cancel,
        const std::string& model_override) override {
        co_return co_await provider.chat_stream_with_tools_async(
            loop, history, tool_reg, tool_choice,
            std::move(handlers), cancel, model_override);
    }

    net::Task<Json> chat_sync(
        net::EventLoop& loop,
        const llm::ConversationHistory& history,
        const capabilities::tool::ToolRegistry& tool_reg,
        const capabilities::tool::ToolChoiceConfig& tool_choice,
        const net::CancellationToken& cancel,
        const std::string& model_override) override {
        co_return co_await provider.chat_with_tools_async(
            loop, history, tool_reg, tool_choice, cancel, model_override);
    }

    const llm::UsageTracker& usage_tracker() const noexcept override {
        return provider.usage_tracker();
    }

    const capabilities::tool::ToolRegistry& default_tools() const noexcept override {
        return tools;
    }
};

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

    memory_.builder_->set_mode(orch_.plans_.current_prompt_mode());
    auto sys_prompt = memory_.builder_->build();
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    execution::LoopConfig loop_config;
    loop_config.max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    loop_config.max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    loop_config.max_parallel_tools = max_parallel_tools_;

    // 创建超时策略：基于 settings 中的配置
    auto timeout_policy = std::make_unique<execution::DefaultTimeoutPolicy>(
        std::chrono::milliseconds(30000),  // 默认 30 秒
        std::unordered_map<std::string, std::chrono::milliseconds>{
            {"execute_command", std::chrono::milliseconds(settings_.agent.command_timeout * 1000)},
            {"search_files", std::chrono::milliseconds(60000)},
            {"grep_content", std::chrono::milliseconds(60000)}
        }
    );

    // 创建服务接口适配器
    auto loop_services = std::make_shared<RuntimeLoopServices>(provider_, tool_reg);

    execution::ExecutionLoop exec_loop(
        loop_config, *loop_services, infra_.core_pool, settings_,
        std::move(timeout_policy));

    // ─── 组装拦截器链 ──────────────────────────────────────────

    // 1. PlanInterceptor：计划模式下拦截写操作
    if (orch_.plans_.is_active()) {
        exec_loop.add_interceptor(
            std::make_unique<execution::PlanInterceptor>(&orch_.plans_));
    }

    // 2. CompactionInterceptor：上下文压缩 + 溢出恢复
    if (auto* compactor = session.compactor()) {
        auto* updater = session.memory_updater();

        // 构建压缩摘要用的 chat_fn（需要 EventLoop + Provider + Tools）
        auto chat_fn = [&loop, &provider = provider_, &tool_reg](
                           const std::string& compaction_prompt) -> std::string {
            llm::ConversationHistory tmp;
            tmp.add_user(std::string(compaction_prompt.data(), compaction_prompt.size()));
            auto response = net::sync_wait(
                loop, provider.chat_with_tools_async(loop, tmp, tool_reg));
            // 解析 OpenAI 格式
            if (response.contains("choices") && response["choices"].is_array() &&
                !response["choices"].empty()) {
                auto choices = response["choices"];
                auto message = choices[0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
            // 解析 Anthropic 格式
            if (response.contains("content") && response["content"].is_array()) {
                for (auto block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        return block.value("text", "");
                    }
                }
            }
            return std::string{};
        };

        auto compaction = std::make_unique<execution::CompactionInterceptor>(
            compactor, updater, std::move(chat_fn));

        // 绑定 force_compact 回调供 ExecutionLoop 在溢出时调用
        auto* compaction_ptr = compaction.get();
        exec_loop.set_context_overflow_handler(
            [compaction_ptr](llm::ConversationHistory& h) -> bool {
                return compaction_ptr->force_compact(h);
            });

        exec_loop.add_interceptor(std::move(compaction));
    }

    // ─── 执行主循环 ────────────────────────────────────────────

    co_return co_await exec_loop.run(loop, history, event_sink, cancel);
}

} // namespace ben_gear::agent::runtime
