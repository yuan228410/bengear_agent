#pragma once

#include "base/utils/json.hpp"
#include "base/net/event_loop.hpp"
#include "base/net/task.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/chat.hpp"
#include "llm/conversation_history.hpp"
#include "llm/provider_interface.hpp"
#include "llm/stream.hpp"
#include "llm/usage.hpp"

#include <memory>

namespace ben_gear::agent::execution {

/// 执行循环后端服务接口
///
/// 抽象 ExecutionLoop 所需的 LLM 和工具服务，使其不依赖具体实现。
/// 由 Runtime 提供默认实现，测试时可替换为 Mock。
class IExecutionLoopServices {
public:
    virtual ~IExecutionLoopServices() = default;

    /// 流式聊天（带工具）
    virtual net::Task<llm::StreamResult> chat_stream(
        net::EventLoop& loop,
        const llm::ConversationHistory& history,
        const capabilities::tool::ToolRegistry& tools,
        const capabilities::tool::ToolChoiceConfig& tool_choice,
        llm::StreamHandlers handlers,
        const net::CancellationToken& cancel,
        const std::string& model_override) = 0;

    /// 非流式聊天（带工具）
    /// @return 原始 JSON 响应
    virtual net::Task<Json> chat_sync(
        net::EventLoop& loop,
        const llm::ConversationHistory& history,
        const capabilities::tool::ToolRegistry& tools,
        const capabilities::tool::ToolChoiceConfig& tool_choice,
        const net::CancellationToken& cancel,
        const std::string& model_override) = 0;

    /// 获取最后一次 usage 统计
    virtual const llm::UsageTracker& usage_tracker() const noexcept = 0;

    /// 获取默认工具注册表
    virtual const capabilities::tool::ToolRegistry& default_tools() const noexcept = 0;
};

} // namespace ben_gear::agent::execution
