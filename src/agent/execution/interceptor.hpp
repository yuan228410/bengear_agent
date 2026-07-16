#pragma once

#include "agent/core/event_sink.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/conversation_history.hpp"

#include <string>
#include <vector>

namespace ben_gear::agent::execution {

/// 拦截器上下文 — 在每个拦截点传递的只读信息
struct InterceptorContext {
    const AgentEventSinks& sinks;
};

/// 执行循环拦截器 — 在 ReAct 循环的关键节点插入行为
///
/// Plan 模式工具过滤、上下文压缩、步数限制等都通过拦截器实现。
/// 拦截器按添加顺序依次调用。
class IInterceptor {
public:
    virtual ~IInterceptor() = default;

    /// LLM 调用之前（可修改 history、system prompt）
    virtual void before_llm(llm::ConversationHistory&, InterceptorContext&) {}

    /// LLM 响应解析后、工具执行前（可过滤/修改 tool_calls）
    /// @param calls     输入：全部工具调用；输出：允许执行的（拦截器从中移除被拦截项）
    /// @param blocked   输出：被拦截的工具调用对应的错误结果（需回传 LLM 保协议完整）
    /// @param history   当前对话历史（只读）
    /// @param ctx       拦截器上下文（含 event sink）
    virtual void before_tools(std::vector<capabilities::tool::ToolCallRequest>& calls,
                              std::vector<capabilities::tool::ToolCallResult>& blocked,
                              const llm::ConversationHistory&,
                              InterceptorContext&) {}

    /// 工具执行后、写入历史前
    virtual void after_tools(const std::vector<capabilities::tool::ToolCallResult>& results,
                             llm::ConversationHistory&,
                             InterceptorContext&) {}

    /// 每轮末尾检查：返回非空字符串表示强制停止的理由
    virtual std::string should_stop(int step, int total_calls,
                                     const llm::ConversationHistory&) {
        return {};
    }
};

} // namespace ben_gear::agent::execution
