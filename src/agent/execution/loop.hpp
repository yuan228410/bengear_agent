#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "agent/execution/interceptor.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/execution/service_interface.hpp"
#include "base/concurrency/thread_pool.hpp"
#include "base/config/settings.hpp"
#include "base/net/event_loop.hpp"
#include "capabilities/tool/registry.hpp"


namespace ben_gear::agent::execution {

/// 执行循环配置
struct LoopConfig {
    int max_steps = 20;
    int max_calls = 50;
    int max_parallel_tools = 5;
};

/// ReAct 执行循环 — Agent 的核心执行原语
///
/// 所有模式（Normal / Plan / SubAgent）都通过 ExecutionLoop 运行。
/// 行为差异通过注入不同的 Interceptor 组合实现。
///
/// 生命周期：ExecutionLoop 不持有 ProviderClient / ToolRegistry 所有权，
/// 仅持有引用，由调用方（Runtime）管理生命周期。
class ExecutionLoop {
public:
    ExecutionLoop(LoopConfig config,
                  IExecutionLoopServices& services,
                  std::shared_ptr<base::concurrency::ThreadPool> pool,
                  const config::Settings& settings,
                  std::unique_ptr<IToolTimeoutPolicy> timeout_policy = nullptr);

    ExecutionLoop(const ExecutionLoop&) = delete;
    ExecutionLoop& operator=(const ExecutionLoop&) = delete;
    ExecutionLoop(ExecutionLoop&&) = default;
    ExecutionLoop& operator=(ExecutionLoop&&) = delete;

    /// 添加拦截器（按添加顺序依次调用，debug 级别输出调用链）
    void add_interceptor(std::unique_ptr<IInterceptor> interceptor);

    /// 设置上下文溢出恢复回调（CompactionInterceptor 提供）
    /// 返回 true 表示恢复成功，调用方应 continue 循环
    void set_context_overflow_handler(
        std::function<bool(llm::ConversationHistory&)> handler) {
        on_context_overflow_ = std::move(handler);
    }

    /// 执行主循环
    ///
    /// 前提：调用方已将 system prompt 和 user message 写入 history。
    net::Task<llm::ChatResult> run(
        net::EventLoop& loop,
        llm::ConversationHistory& history,
        const AgentEventSinks& sinks,
        const net::CancellationToken& cancel);

    /// 执行主循环（带 tool_override，用于 SubAgent 等场景）
    net::Task<llm::ChatResult> run(
        net::EventLoop& loop,
        llm::ConversationHistory& history,
        const AgentEventSinks& sinks,
        const net::CancellationToken& cancel,
        const capabilities::tool::ToolRegistry& tool_override);

private:
    net::Task<llm::ChatResult> run_stream(
        net::EventLoop& loop,
        llm::ConversationHistory& history,
        const AgentEventSinks& sinks, const net::CancellationToken& cancel,
        const capabilities::tool::ToolRegistry& tool_reg);

    net::Task<llm::ChatResult> run_sync(
        net::EventLoop& loop,
        llm::ConversationHistory& history,
        const AgentEventSinks& sinks, const net::CancellationToken& cancel,
        const capabilities::tool::ToolRegistry& tool_reg);

    /// 执行工具调用（含并行分批、拦截器调用、结果通知）
    std::vector<capabilities::tool::ToolCallResult> execute_tools(
        std::vector<capabilities::tool::ToolCallRequest>& calls,
        const capabilities::tool::ToolRegistry& tool_reg,
        llm::ConversationHistory& history,
        const AgentEventSinks& sinks,
        int step = 0,
        int total_calls = 0);

    /// 应用拦截器的 before_tools，返回被过滤掉的 blocked_results
    void apply_before_tools(
        std::vector<capabilities::tool::ToolCallRequest>& calls,
        std::vector<capabilities::tool::ToolCallResult>& blocked,
        const llm::ConversationHistory& history,
        LoopSnapshot& snapshot);

    /// debug 级别输出当前拦截器链
    void log_interceptor_chain() const;

    LoopConfig config_;
    IExecutionLoopServices& services_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
    const config::Settings& settings_;
    std::vector<std::unique_ptr<IInterceptor>> interceptors_;
    // 上下文溢出恢复（CompactionInterceptor 绑定的回调）
    std::function<bool(llm::ConversationHistory&)> on_context_overflow_;
    // 工具超时策略
    std::unique_ptr<IToolTimeoutPolicy> timeout_policy_;
};

} // namespace ben_gear::agent::execution
