#include "agent/runtime/runtime.hpp"
#include "agent/execution/loop.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/execution/service_interface.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/execution/interceptors/plan_interceptor.hpp"
#include "agent/execution/interceptors/compaction_interceptor.hpp"
#include "agent/core/event_sink.hpp"
#include "base/log/logger.hpp"
#include "llm/provider_client.hpp"  // complete type for RuntimeLoopServices inline methods

// 通过 ServiceRegistry 访问子服务所需的类型头文件
#include "agent/runtime/memory_context.hpp"          // IMemoryContext
#include "agent/runtime/orchestration_context.hpp"   // IOrchestrationContext
#include "base/concurrency/thread_pool.hpp"          // ThreadPool 用于 ExecutionLoop
#include "agent/runtime/service_bundles.hpp"          // InfrastructureServices

namespace ben_gear::agent::runtime {

/// IExecutionLoopServices 默认实现 — 通过 services().resolve<T>() 获取 provider/tools
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
    auto& loop = config.loop;
    auto& session = config.session;
    auto prompt = std::move(config.prompt);
    auto& cancel = config.cancel;

    auto& svc = services();
    auto* event_bus = svc.resolve<base::EventBus>();

    // 获取 ToolRegistry（支持 tool_override 注入）
    const auto& tool_reg = *svc.resolve<capabilities::tool::ToolRegistry>();
    auto& history = session.history();

    // 构建系统提示词
    auto* mem_ctx = svc.resolve<IMemoryContext>();
    mem_ctx->builder()->set_mode(
        svc.resolve<IOrchestrationContext>()->plans().current_prompt_mode());
    auto sys_prompt = mem_ctx->builder()->build();
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    // 执行配置
    auto& settings = *svc.resolve<config::Settings>();

    execution::LoopConfig loop_config;
    loop_config.max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    loop_config.max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    loop_config.max_parallel_tools = max_parallel_tools_;

    // 超时策略：基于 settings 中的配置
    auto timeout_policy = std::make_unique<execution::DefaultTimeoutPolicy>(
        std::chrono::milliseconds(30000),  // 默认 30 秒
        std::unordered_map<std::string, std::chrono::milliseconds>{
            {"execute_command", std::chrono::milliseconds(settings.agent.command_timeout * 1000)},
            {"search_files", std::chrono::milliseconds(60000)},
            {"grep_content", std::chrono::milliseconds(60000)}
        }
    );

    // 创建 IExecutionLoopServices 适配器
    auto& provider = *svc.resolve<llm::ProviderClient>();
    auto loop_services = std::make_shared<RuntimeLoopServices>(provider, tool_reg);

    // ThreadPool 通过 InfrastructureServices 获取（保留 shared_ptr 语义）
    auto core_pool = svc.resolve<InfrastructureServices>()->core_pool;
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
