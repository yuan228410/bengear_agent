#pragma once

#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace ben_gear::llm {

/// 工具调用管理器（处理工具调用的完整流程）
/// 线程池通过 shared_ptr 共享，避免每个 Agent 实例独占线程池
class ToolCallManager {
public:
    /// 构造函数（共享线程池）
    /// @param registry 工具注册表引用
    /// @param pool 共享线程池（来自 SharedResources）
    /// @param timeout 工具执行超时时间
    explicit ToolCallManager(const ToolRegistry& registry,
                             std::shared_ptr<base::concurrency::ThreadPool> pool,
                             std::chrono::milliseconds timeout = std::chrono::seconds(30))
        : registry_(registry), timeout_(timeout), pool_(std::move(pool)) {}

    /// 从 OpenAI 响应中提取工具调用
    std::vector<ToolCallRequest> extract_openai_tool_calls(const Json& response) const {
        std::vector<ToolCallRequest> calls;

        if (!response.contains("choices") || !response["choices"].is_array()) {
            return calls;
        }

        for (const auto& choice : response["choices"]) {
            if (!choice.contains("message")) continue;

            const auto& message = choice["message"];
            if (!message.contains("tool_calls")) continue;

            for (const auto& tool_call : message["tool_calls"]) {
                try {
                    calls.push_back(ToolCallRequest::from_openai(tool_call));
                } catch (const std::exception& e) {
                    log::error_fmt("failed to parse openai tool call: {}", e.what());
                }
            }
        }

        return calls;
    }

    /// 从 Anthropic 响应中提取工具调用
    std::vector<ToolCallRequest> extract_anthropic_tool_calls(const Json& response) const {
        std::vector<ToolCallRequest> calls;

        if (!response.contains("content") || !response["content"].is_array()) {
            return calls;
        }

        for (const auto& block : response["content"]) {
            if (block.value("type", "") != "tool_use") continue;

            try {
                calls.push_back(ToolCallRequest::from_anthropic(block));
            } catch (const std::exception& e) {
                log::error_fmt("failed to parse anthropic tool call: {}", e.what());
            }
        }

        return calls;
    }

    /// 执行工具调用（带超时控制）
    ToolCallResult execute_tool(const ToolCallRequest& request) const {
        auto future = pool_->submit([this, &request]() -> ToolCallResult {
            return execute_tool_impl(request);
        });

        if (future.wait_for(timeout_) == std::future_status::timeout) {
            log::warn_fmt("tool execution timeout: name={} timeout_ms={}",
                          request.name, timeout_.count());
            return {request.id, request.name,
                    container::String("Error: Tool execution timeout"), false};
        }

        return future.get();
    }

    /// 批量执行工具调用（顺序，带超时）
    std::vector<ToolCallResult> execute_tools(const std::vector<ToolCallRequest>& requests) const {
        std::vector<ToolCallResult> results;
        results.reserve(requests.size());

        for (const auto& request : requests) {
            results.push_back(execute_tool(request));
        }

        return results;
    }

    /// 批量并行执行工具调用
    /// 每个工具独立执行，结果按输入顺序返回，通过共享线程池复用线程
    std::vector<ToolCallResult> execute_tools_parallel(const std::vector<ToolCallRequest>& requests) const {
        if (requests.empty()) return {};
        if (requests.size() == 1) {
            return {execute_tool(requests[0])};
        }

        std::vector<std::future<ToolCallResult>> futures;
        futures.reserve(requests.size());

        for (const auto& req : requests) {
            futures.push_back(pool_->submit([this, &req]() {
                return execute_tool_impl(req);
            }));
        }

        std::vector<ToolCallResult> results;
        results.reserve(requests.size());
        for (auto& f : futures) {
            results.push_back(f.get());
        }

        return results;
    }

    /// 构建 OpenAI 工具结果消息
    Json build_openai_tool_results(const std::vector<ToolCallResult>& results) const {
        Json messages = Json::array();
        for (const auto& result : results) {
            messages.push_back(Json{
                {"role", "tool"},
                {"tool_call_id", result.tool_call_id},
                {"content", result.output}
            });
        }
        return messages;
    }

    /// 构建 Anthropic 工具结果消息
    Json build_anthropic_tool_results(const std::vector<ToolCallResult>& results) const {
        Json content = Json::array();
        for (const auto& result : results) {
            content.push_back(Json{
                {"type", "tool_result"},
                {"tool_use_id", result.tool_call_id},
                {"content", result.output}
            });
        }

        return Json{
            {"role", "user"},
            {"content", content}
        };
    }

    /// 检查响应是否包含工具调用
    static bool has_tool_calls(const Json& response, Provider provider) {
        if (provider == Provider::openai) {
            if (!response.contains("choices") || !response["choices"].is_array()) {
                return false;
            }
            for (const auto& choice : response["choices"]) {
                if (choice.contains("message") &&
                    choice["message"].contains("tool_calls") &&
                    !choice["message"]["tool_calls"].empty()) {
                    return true;
                }
            }
            return false;
        } else {  // anthropic
            if (!response.contains("content") || !response["content"].is_array()) {
                return false;
            }
            for (const auto& block : response["content"]) {
                if (block.value("type", "") == "tool_use") {
                    return true;
                }
            }
            return false;
        }
    }

private:
    /// 实际执行工具调用（不含超时逻辑，供线程池内部调用）
    ToolCallResult execute_tool_impl(const ToolCallRequest& request) const {
        ToolCallResult result;
        result.tool_call_id = request.id;
        result.name = request.name;

        auto exec = registry_.execute(request.name, request.arguments);
        result.success = exec.success;
        result.output = exec.success
            ? exec.output
            : container::String((std::string("Error: ") + std::string(exec.error.c_str())).c_str());

        return result;
    }

    const ToolRegistry& registry_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;  // 共享线程池，来自 SharedResources
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolCallManager = llm::ToolCallManager;
}  // namespace ben_gear
