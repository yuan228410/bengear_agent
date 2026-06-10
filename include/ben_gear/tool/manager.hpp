#pragma once

#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/workflow/namespace.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace ben_gear::llm {

/// 工具调用管理器（处理工具调用的完整流程）
class ToolCallManager {
public:
    explicit ToolCallManager(
        const ToolRegistry& registry,
        std::shared_ptr<base::concurrency::ThreadPool> pool,
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    explicit ToolCallManager(
        const ToolRegistry& registry,
        std::shared_ptr<base::concurrency::ThreadPool> pool,
        std::chrono::milliseconds timeout,
        std::shared_ptr<const void> context);

    void set_tool_timeout(const container::String& tool_name,
                          std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_tool_timeout(
        const container::String& tool_name) const;

    /// 从 OpenAI 响应中提取工具调用
    std::vector<ToolCallRequest> extract_openai_tool_calls(
        const Json& response) const;

    /// 从 Anthropic 响应中提取工具调用
    std::vector<ToolCallRequest> extract_anthropic_tool_calls(
        const Json& response) const;

    /// 执行工具调用（带超时控制）
    ToolCallResult execute_tool(const ToolCallRequest& request) const;

    /// 批量顺序执行
    std::vector<ToolCallResult> execute_tools(
        const std::vector<ToolCallRequest>& requests) const;

    /// 批量并行执行
    std::vector<ToolCallResult> execute_tools_parallel(
        const std::vector<ToolCallRequest>& requests) const;

    /// 构建 OpenAI 工具结果消息
    Json build_openai_tool_results(
        const std::vector<ToolCallResult>& results) const;

    /// 构建 Anthropic 工具结果消息
    Json build_anthropic_tool_results(
        const std::vector<ToolCallResult>& results) const;

    /// 检查响应是否包含工具调用
    static bool has_tool_calls(const Json& response, Provider provider);

private:
    const ToolRegistry& registry_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
    container::Map<container::String, std::chrono::milliseconds>
        tool_timeouts_;
    std::shared_ptr<const void> context_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolCallManager = llm::ToolCallManager;
}  // namespace ben_gear
