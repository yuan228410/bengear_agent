#pragma once

#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ben_gear::llm {

/// 工具调用管理器（处理工具调用的完整流程）
/// 线程池通过 shared_ptr 共享，避免每个 Agent 实例独占线程池
class ToolCallManager {
public:
    /// 构造函数（核心调度线程池）
    /// @param registry 工具注册表引用
    /// @param pool 核心调度线程池（来自 SharedResources）
    /// @param timeout 工具执行超时时间
    explicit ToolCallManager(const ToolRegistry& registry,
                             std::shared_ptr<base::concurrency::ThreadPool> pool,
                             std::chrono::milliseconds timeout = std::chrono::seconds(30))
        : registry_(registry), timeout_(timeout), pool_(std::move(pool)) {}

    /// 构造函数（带生命周期上下文，防止任务执行时宿主对象析构）
    /// @param context 共享资源指针，延长 SharedResources 生命周期直到所有任务完成
    explicit ToolCallManager(const ToolRegistry& registry,
                             std::shared_ptr<base::concurrency::ThreadPool> pool,
                             std::chrono::milliseconds timeout,
                             std::shared_ptr<const void> context)
        : registry_(registry), timeout_(timeout), pool_(std::move(pool)), context_(std::move(context)) {}

    /// 设置指定工具的超时覆盖（工作流等长耗时工具需要更长超时）
    void set_tool_timeout(const container::String& tool_name, std::chrono::milliseconds timeout) {
        tool_timeouts_[std::string(tool_name.c_str())] = timeout;
    }

    /// 获取工具的实际超时时间（优先使用覆盖值）
    std::chrono::milliseconds get_tool_timeout(const container::String& tool_name) const {
        auto it = tool_timeouts_.find(std::string(tool_name.c_str()));
        return it != tool_timeouts_.end() ? it->second : timeout_;
    }

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
    /// 注意：按值捕获 request，避免异步执行时悬空引用
    ToolCallResult execute_tool(const ToolCallRequest& request) const {
        // 捕获主线程的命名空间，传播到工作线程（thread_local 不自动传播）
        const auto saved_ns = workflow::WorkflowEngine::get_current_namespace();
        // 捕获 registry 指针和 context_（shared_ptr<SharedResources>），确保任务执行期间宿主对象存活
        // 不捕获 this，避免超时后 Agent 析构导致 use-after-free
        // context_ 持有 SharedResources 的 shared_ptr，保证 registry_ 引用有效
        const auto* reg_ptr = &registry_;
        auto ctx = context_;
        auto future = pool_->submit([reg_ptr, request, saved_ns, ctx]() -> ToolCallResult {
            workflow::WorkflowEngine::NamespaceGuard ns_guard(saved_ns);
            ToolCallResult result;
            result.tool_call_id = request.id;
            result.name = request.name;
            auto exec = reg_ptr->execute(request.name, request.arguments);
            result.success = exec.success;
            result.output = exec.success
                ? exec.output
                : container::String((std::string("Error: ") + std::string(exec.error.c_str())).c_str());
            return result;
        });

        auto tool_timeout = get_tool_timeout(request.name);
        if (future.wait_for(tool_timeout) == std::future_status::timeout) {
            log::warn_fmt("tool execution timeout: name={} timeout_ms={}",
                          request.name, tool_timeout.count());
            return {request.id, request.name,
                    container::String("Error: Tool execution timeout"), false};
        }

        return future.get();
    }

    /// 批量执行工具调用（顺序，带超时）
    /// 注意：此方法为顺序执行，适用于需要严格顺序保证的场景
    /// 如需并行执行，请使用 execute_tools_parallel()
    std::vector<ToolCallResult> execute_tools(const std::vector<ToolCallRequest>& requests) const {
        std::vector<ToolCallResult> results;
        log::debug_fmt("tool batch execute: count={}", requests.size());
        results.reserve(requests.size());

        for (const auto& request : requests) {
            results.push_back(execute_tool(request));
        }

        return results;
    }

    /// 批量并行执行工具调用
    /// 每个工具独立执行，结果按输入顺序返回，通过核心调度线程池复用线程
    /// 注意：按值捕获 request，避免异步执行时悬空引用
    std::vector<ToolCallResult> execute_tools_parallel(const std::vector<ToolCallRequest>& requests) const {
        if (requests.empty()) return {};
        log::debug_fmt("tool parallel execute: count={}", requests.size());
        if (requests.size() == 1) {
            return {execute_tool(requests[0])};
        }

        std::vector<std::future<ToolCallResult>> futures;
        futures.reserve(requests.size());

        const auto saved_ns = workflow::WorkflowEngine::get_current_namespace();
        const auto* reg_ptr = &registry_;
        auto ctx = context_;
        for (const auto& req : requests) {
            futures.push_back(pool_->submit([reg_ptr, req, saved_ns, ctx]() -> ToolCallResult {
                workflow::WorkflowEngine::NamespaceGuard ns_guard(saved_ns);
                ToolCallResult result;
                result.tool_call_id = req.id;
                result.name = req.name;
                auto exec = reg_ptr->execute(req.name, req.arguments);
                result.success = exec.success;
                result.output = exec.success
                    ? exec.output
                    : container::String((std::string("Error: ") + std::string(exec.error.c_str())).c_str());
                return result;
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

    const ToolRegistry& registry_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;  // 核心调度线程池（工具调用+轻量级任务+核心业务）
    std::unordered_map<std::string, std::chrono::milliseconds> tool_timeouts_;  // 工具级超时覆盖
    std::shared_ptr<const void> context_;  // 生命周期上下文（shared_ptr<SharedResources>，防止 use-after-free）
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolCallManager = llm::ToolCallManager;
}  // namespace ben_gear
