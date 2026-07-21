#pragma once

#include "base/core/event_bus.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/conversation_history.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace ben_gear::agent::execution {

/// 循环快照 — 在每个拦截点传递的运行时状态
struct LoopSnapshot {
    base::EventBus& event_bus;

    // 循环状态
    int step = 0;
    int total_calls = 0;
    int max_steps = 0;
    int max_calls = 0;

    // 性能统计
    std::chrono::steady_clock::time_point loop_start;
    std::chrono::milliseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loop_start);
    }
};

/// 执行循环拦截器 — 在 ReAct 循环的关键节点插入行为
class IInterceptor {
public:
    virtual ~IInterceptor() = default;
    virtual const char* name() const noexcept = 0;

    /// LLM 调用之前（可修改 history、system prompt）
    virtual void before_llm(llm::ConversationHistory&, LoopSnapshot&) {}

    /// LLM 响应解析后、工具执行前（可过滤/修改 tool_calls）
    virtual void before_tools(std::vector<acp::ToolCallRequest>&,
                              std::vector<acp::ToolCallResult>&,
                              const llm::ConversationHistory&,
                              LoopSnapshot&) {}

    /// 工具执行后、写入历史前
    virtual void after_tools(const std::vector<acp::ToolCallResult>&,
                             llm::ConversationHistory&,
                             LoopSnapshot&) {}

    /// 每轮末尾检查
    virtual std::string should_stop(const LoopSnapshot& /*snapshot*/,
                                    const llm::ConversationHistory&) {
        return {};
    }
};

} // namespace ben_gear::agent::execution
