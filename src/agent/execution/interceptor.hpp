#pragma once

#include "agent/core/event_sink.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/conversation_history.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace ben_gear::agent::execution {

/// 循环快照 — 在每个拦截点传递的运行时状态
///
/// 替代原来的 InterceptorContext，提供更丰富的循环状态信息，
/// 让拦截器无需额外参数即可做出决策。
struct LoopSnapshot {
    const AgentEventSinks& sinks;

    // 循环状态
    int step = 0;                      // 当前步数（0-indexed）
    int total_calls = 0;               // 累计工具调用次数
    int max_steps = 0;                 // 最大步数限制
    int max_calls = 0;                 // 最大调用次数限制

    // 性能统计
    std::chrono::steady_clock::time_point loop_start;  // 循环开始时间
    std::chrono::milliseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loop_start);
    }
};

/// 向后兼容别名
using InterceptorContext = LoopSnapshot;

/// 执行循环拦截器 — 在 ReAct 循环的关键节点插入行为
///
/// Plan 模式工具过滤、上下文压缩、步数限制等都通过拦截器实现。
/// 拦截器按添加顺序依次调用。
class IInterceptor {
public:
    virtual ~IInterceptor() = default;
    /// 拦截器名称（用于诊断日志）
    virtual const char* name() const noexcept = 0;


    /// LLM 调用之前（可修改 history、system prompt）
    virtual void before_llm(llm::ConversationHistory&, InterceptorContext&) {}

    /// LLM 响应解析后、工具执行前（可过滤/修改 tool_calls）
    /// @param calls     输入：全部工具调用；输出：允许执行的（拦截器从中移除被拦截项）
    /// @param blocked   输出：被拦截的工具调用对应的错误结果（需回传 LLM 保协议完整）
    /// @param history   当前对话历史（只读）
    /// @param ctx       拦截器上下文（含 event sink）
    virtual void before_tools(std::vector<acp::ToolCallRequest>&,
                              std::vector<acp::ToolCallResult>&,
                              const llm::ConversationHistory&,
                              InterceptorContext&) {}

    /// 工具执行后、写入历史前
    virtual void after_tools(const std::vector<acp::ToolCallResult>&,
                             llm::ConversationHistory&,
                             InterceptorContext&) {}

    /// 每轮末尾检查：返回非空字符串表示强制停止的理由
    virtual std::string should_stop(const LoopSnapshot& /*snapshot*/,
                                    const llm::ConversationHistory&) {
        return {};
    }
};

} // namespace ben_gear::agent::execution
