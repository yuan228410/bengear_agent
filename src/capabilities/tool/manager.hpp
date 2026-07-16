#pragma once

#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "base/concurrency/thread_pool.hpp"
#include "workflow/namespace.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace ben_gear::capabilities::tool {

/// 工具调用管理器（处理工具调用的完整流程）
class ToolCallManager {
public:
    explicit ToolCallManager(
        const ToolRegistry& registry,
        std::shared_ptr<base::concurrency::ThreadPool> pool,
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    void set_tool_timeout(const std::string& tool_name,
                          std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_tool_timeout(
        const std::string& tool_name) const;


    /// 执行工具调用（带超时控制）
    ToolCallResult execute_tool(const ToolCallRequest& request) const;

    /// 批量顺序执行
    std::vector<ToolCallResult> execute_tools(
        const std::vector<ToolCallRequest>& requests) const;

    /// 批量并行执行
    std::vector<ToolCallResult> execute_tools_parallel(
        const std::vector<ToolCallRequest>& requests) const;


private:
    const ToolRegistry& registry_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
    std::unordered_map<std::string, std::chrono::milliseconds>
        tool_timeouts_;
};

}  // namespace ben_gear::capabilities::tool

namespace ben_gear {
using ToolCallManager = capabilities::tool::ToolCallManager;
}  // namespace ben_gear
