#include "agent/runtime/session_runner.hpp"
#include <chrono>
#include "agent/execution/loop.hpp"
#include "workspace/session.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/execution/service_interface.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/execution/interceptors/plan_interceptor.hpp"
#include "agent/execution/interceptors/compaction_interceptor.hpp"
#include "log/logger.hpp"
#include "llm/provider_client.hpp"

#include "agent/runtime/memory_context.hpp"
#include "agent/runtime/orchestration_context.hpp"
#include "concurrency/thread_pool.hpp"
#include "agent/runtime/service_bundles.hpp"

namespace ben_gear::agent::runtime {

/// IExecutionLoopServices 默认实现 — 通过 ServiceRegistry 获取 provider/tools
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

net::Task<llm::ChatResult> SessionRunner::run(
    base::ServiceRegistry& svc,
    RunConfig config,
    int max_tool_steps,
    int max_tool_calls,
    int max_parallel_tools) {

    auto& loop = config.loop;
    auto& session = config.session;
    auto prompt = std::move(config.prompt);
    auto& cancel = config.cancel;

    auto* event_bus = svc.resolve<base::EventBus>();

    // 获取 ToolRegistry
    auto& tool_reg = svc.resolve_ref<capabilities::tool::ToolRegistry>();
    auto& history = session.history();

    // 构建系统提示词
    auto& mem_ctx = svc.resolve_ref<IMemoryContext>();
    mem_ctx.builder()->set_mode(
        svc.resolve_ref<IOrchestrationContext>().plans().current_prompt_mode());
    auto sys_prompt = mem_ctx.builder()->build();
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    // 执行配置
    auto& settings = svc.resolve_ref<config::Settings>();

    execution::LoopConfig loop_config;
    loop_config.max_steps = max_tool_steps > 0 ? max_tool_steps : 20;
    loop_config.max_calls = max_tool_calls > 0 ? max_tool_calls : 50;
    loop_config.max_parallel_tools = max_parallel_tools;

    // 超时策略
    auto timeout_policy = std::make_unique<execution::DefaultTimeoutPolicy>(
        std::chrono::milliseconds(30000),
        std::unordered_map<std::string, std::chrono::milliseconds>{
            {"execute_command", std::chrono::milliseconds(settings.agent.command_timeout * 1000)},
            {"search_files", std::chrono::milliseconds(60000)},
            {"grep_content", std::chrono::milliseconds(60000)}
        }
    );

    // 创建 IExecutionLoopServices 适配器
    auto& provider = svc.resolve_ref<llm::ProviderClient>();
    auto loop_services = std::make_shared<RuntimeLoopServices>(provider, tool_reg);

    // ThreadPool 通过 InfrastructureServices 获取
    auto core_pool = svc.resolve_ref<InfrastructureServices>().core_pool;
    execution::ExecutionLoop exec_loop(
        loop_config, *loop_services,
        core_pool,
        settings,
        std::move(timeout_policy));

    auto& plans = svc.resolve<IOrchestrationContext>()->plans();

    // ─── 组装拦截器链 ──────────────────────────────────────────

    // 1. PlanInterceptor：计划模式下拦截写操作
    if (plans.is_active()) {
        exec_loop.add_interceptor(
            std::make_unique<execution::PlanInterceptor>(&plans));
    }

    // 2. CompactionInterceptor：上下文压缩 + 溢出恢复
    if (auto* compactor = session.compactor()) {
        auto* updater = session.memory_updater();

        // 构建压缩摘要用的 chat_fn（需要 EventLoop + Provider + Tools）
        auto chat_fn = [&loop, &provider, &tool_reg](
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

    co_return co_await exec_loop.run(loop, history, *event_bus, cancel);
}

} // namespace ben_gear::agent::runtime
