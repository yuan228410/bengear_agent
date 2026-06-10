#include "ben_gear/tool/manager.hpp"

namespace ben_gear::llm {


ToolCallManager::ToolCallManager(
    const ToolRegistry& registry,
    std::shared_ptr<base::concurrency::ThreadPool> pool,
    std::chrono::milliseconds timeout)
    : registry_(registry), timeout_(timeout), pool_(std::move(pool)) {}

ToolCallManager::ToolCallManager(
    const ToolRegistry& registry,
    std::shared_ptr<base::concurrency::ThreadPool> pool,
    std::chrono::milliseconds timeout,
    std::shared_ptr<const void> context)
    : registry_(registry), timeout_(timeout), pool_(std::move(pool)),
      context_(std::move(context)) {}

void ToolCallManager::set_tool_timeout(
    const container::String& tool_name,
    std::chrono::milliseconds timeout) {
    tool_timeouts_[tool_name] = timeout;
}

std::chrono::milliseconds ToolCallManager::get_tool_timeout(
    const container::String& tool_name) const {
    auto it = tool_timeouts_.find(tool_name);
    return it != tool_timeouts_.end() ? it->second : timeout_;
}

std::vector<ToolCallRequest>
ToolCallManager::extract_openai_tool_calls(const Json& response) const {
    std::vector<ToolCallRequest> calls;

    if (!response.contains("choices") ||
        !response["choices"].is_array()) {
        return calls;
    }

    for (auto choice : response["choices"]) {
        if (!choice.contains("message")) continue;
        auto message = choice["message"];
        if (!message.contains("tool_calls")) continue;
        for (auto tool_call : message["tool_calls"]) {
            try {
                calls.push_back(ToolCallRequest::from_openai(tool_call));
            } catch (const std::exception& e) {
                log::error_fmt("failed to parse openai tool call: {}",
                               e.what());
            }
        }
    }

    return calls;
}

std::vector<ToolCallRequest>
ToolCallManager::extract_anthropic_tool_calls(
    const Json& response) const {
    std::vector<ToolCallRequest> calls;

    if (!response.contains("content") ||
        !response["content"].is_array()) {
        return calls;
    }

    for (auto block : response["content"]) {
        if (block.value("type", "") != "tool_use") continue;
        try {
            calls.push_back(ToolCallRequest::from_anthropic(block));
        } catch (const std::exception& e) {
            log::error_fmt(
                "failed to parse anthropic tool call: {}", e.what());
        }
    }

    return calls;
}

ToolCallResult ToolCallManager::execute_tool(
    const ToolCallRequest& request) const {
    const auto saved_ns = workflow::get_current_namespace();
    const auto* reg_ptr = &registry_;
    auto ctx = context_;
    auto future = pool_->submit(
        [reg_ptr, request, saved_ns, ctx]() -> ToolCallResult {
            workflow::NamespaceGuard ns_guard(saved_ns);
            ToolCallResult result;
            result.tool_call_id = request.id;
            result.name = request.name;
            auto exec = reg_ptr->execute(request.name, request.arguments);
            result.success = exec.success;
            result.output =
                exec.success
                    ? exec.output
                    : container::String("Error: ") + exec.error;
            return result;
        });

    auto status = future.wait_for(get_tool_timeout(request.name));
    if (status == std::future_status::timeout) {
        log::error_fmt("tool execution timeout: name={}, timeout={}ms",
                       request.name,
                       get_tool_timeout(request.name).count());
        return {request.id, request.name,
                container::String("Error: Tool execution timeout"), false};
    }

    return future.get();
}

std::vector<ToolCallResult> ToolCallManager::execute_tools(
    const std::vector<ToolCallRequest>& requests) const {
    std::vector<ToolCallResult> results;
    log::debug_fmt("tool batch execute: count={}", requests.size());
    results.reserve(requests.size());

    for (auto request : requests) {
        results.push_back(execute_tool(request));
    }

    return results;
}

std::vector<ToolCallResult>
ToolCallManager::execute_tools_parallel(
    const std::vector<ToolCallRequest>& requests) const {
    if (requests.empty()) return {};
    log::debug_fmt("tool parallel execute: count={}", requests.size());
    if (requests.size() == 1) {
        return {execute_tool(requests[0])};
    }

    std::vector<std::future<ToolCallResult>> futures;
    futures.reserve(requests.size());

    const auto saved_ns = workflow::get_current_namespace();
    const auto* reg_ptr = &registry_;
    auto ctx = context_;
    for (auto req : requests) {
        futures.push_back(pool_->submit(
            [reg_ptr, req, saved_ns, ctx]() -> ToolCallResult {
                workflow::NamespaceGuard ns_guard(saved_ns);
                ToolCallResult result;
                result.tool_call_id = req.id;
                result.name = req.name;
                auto exec =
                    reg_ptr->execute(req.name, req.arguments);
                result.success = exec.success;
                result.output =
                    exec.success
                        ? exec.output
                        : container::String("Error: ") + exec.error;
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

Json ToolCallManager::build_openai_tool_results(
    const std::vector<ToolCallResult>& results) const {
    Json messages = Json::array();
    for (auto result : results) {
        messages.push_back(
            Json{{"role", "tool"},
                 {"tool_call_id", result.tool_call_id},
                 {"content", result.output}});
    }
    return messages;
}

Json ToolCallManager::build_anthropic_tool_results(
    const std::vector<ToolCallResult>& results) const {
    Json content = Json::array();
    for (auto result : results) {
        content.push_back(
            Json{{"type", "tool_result"},
                 {"tool_use_id", result.tool_call_id},
                 {"content", result.output}});
    }

    return Json{{"role", "user"}, {"content", content}};
}

bool ToolCallManager::has_tool_calls(const Json& response,
                                      Provider provider) {
    if (provider == Provider::openai) {
        if (!response.contains("choices") ||
            !response["choices"].is_array()) {
            return false;
        }
        for (auto choice : response["choices"]) {
            if (choice.contains("message") &&
                choice["message"].contains("tool_calls") &&
                !choice["message"]["tool_calls"].empty()) {
                return true;
            }
        }
        return false;
    } else {
        if (!response.contains("content") ||
            !response["content"].is_array()) {
            return false;
        }
        for (auto block : response["content"]) {
            if (block.value("type", "") == "tool_use") {
                return true;
            }
        }
        return false;
    }
}

}  // namespace ben_gear::llm
