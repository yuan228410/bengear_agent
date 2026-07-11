#include "tool/manager.hpp"
#include "base/log/logger.hpp"

#include <string>
#include <string_view>

namespace ben_gear::llm {

namespace {
constexpr size_t kMaxToolOutputChars = 200000;

ToolCallResult make_tool_error(const ToolCallRequest& request, std::string_view message) {
    ToolCallResult result;
    result.tool_call_id = request.id;
    result.name = request.name;
    result.output = container::String("Error: ");
    result.output.append(message);
    result.success = false;
    return result;
}

ToolCallResult make_tool_json_error(const ToolCallRequest& request, const Json& error) {
    ToolCallResult result;
    result.tool_call_id = request.id;
    result.name = request.name;
    result.output = error.dump();
    result.success = false;
    return result;
}

void truncate_tool_output(ToolCallResult& result) {
    if (result.output.size() <= kMaxToolOutputChars) return;
    const auto original_size = result.output.size();
    result.output.resize(kMaxToolOutputChars);
    result.output.append("\n...[truncated, original_size=");
    auto size_text = std::to_string(original_size);
    result.output.append(std::string_view(size_text.data(), size_text.size()));
    result.output.append("]");
}

bool output_is_structured_failure(const container::String& output) {
    try {
        auto json = Json::parse(std::string(output.data(), output.size()));
        return json.is_object() && json.contains("success") && !json.value("success", true);
    } catch (...) {
        return false;
    }
}
}

ToolCallManager::ToolCallManager(
    const ToolRegistry& registry,
    std::shared_ptr<base::concurrency::ThreadPool> pool,
    std::chrono::milliseconds timeout)
    : registry_(registry), timeout_(timeout), pool_(std::move(pool)) {}

ToolCallManager::ToolCallManager(
    const ToolRegistry& registry,
    std::shared_ptr<base::concurrency::ThreadPool> pool,
    std::chrono::milliseconds timeout,
    std::shared_ptr<const permission::ToolPermissionProvider> permission_provider)
    : registry_(registry), timeout_(timeout), pool_(std::move(pool)),
      permission_provider_(std::move(permission_provider)) {}

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
    if (permission_provider_) {
        auto decision = permission_provider_->evaluate_tool_permission(request.name, request.arguments);
        if (!decision.allowed()) {
            ToolCallResult result;
            result.tool_call_id = request.id;
            result.name = request.name;
            result.output = permission::to_json(decision).dump();
            result.success = false;
            return result;
        }
        auto before = permission_provider_->before_tool_execution(request.name, request.arguments);
        if (!before.value("success", true)) {
            return make_tool_json_error(request, before);
        }
    }

    const auto saved_ns = workflow::get_current_namespace();
    const auto* reg_ptr = &registry_;
    std::future<ToolCallResult> future;
    try {
        future = pool_->submit(
            [reg_ptr, request, saved_ns]() -> ToolCallResult {
                workflow::NamespaceGuard ns_guard(saved_ns);
                ToolCallResult result;
                result.tool_call_id = request.id;
                result.name = request.name;
                auto exec = reg_ptr->execute(request.name, request.arguments);
                result.success = exec.success && !output_is_structured_failure(exec.output);
                result.output =
                    exec.success
                        ? exec.output
                        : container::String("Error: ") + exec.error;
                return result;
            });
    } catch (const std::exception& e) {
        log::error_fmt("tool execution submit failed: name={}, error={}", request.name, e.what());
        return make_tool_error(request, e.what());
    }

    auto status = future.wait_for(get_tool_timeout(request.name));
    if (status == std::future_status::timeout) {
        log::error_fmt("tool execution timeout: name={}, timeout={}ms",
                       request.name,
                       get_tool_timeout(request.name).count());
        return {request.id, request.name,
                container::String("Error: Tool execution timeout"), false};
    }

    try {
        auto result = future.get();
        truncate_tool_output(result);
        return result;
    } catch (const std::exception& e) {
        log::error_fmt("tool execution failed: name={}, error={}", request.name, e.what());
        return make_tool_error(request, e.what());
    }
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
    std::vector<ToolCallRequest> future_requests;
    future_requests.reserve(requests.size());
    std::vector<ToolCallResult> immediate_results;

    const auto saved_ns = workflow::get_current_namespace();
    const auto* reg_ptr = &registry_;
    std::vector<ToolCallRequest> submitted;
    submitted.reserve(requests.size());
    for (auto req : requests) {
        if (permission_provider_) {
            auto decision = permission_provider_->evaluate_tool_permission(req.name, req.arguments);
            if (!decision.allowed()) {
                ToolCallResult result;
                result.tool_call_id = req.id;
                result.name = req.name;
                result.output = permission::to_json(decision).dump();
                result.success = false;
                immediate_results.push_back(std::move(result));
                submitted.push_back(req);
                continue;
            }
        }
        if (permission_provider_) {
            auto before = permission_provider_->before_tool_execution(req.name, req.arguments);
            if (!before.value("success", true)) {
                immediate_results.push_back(make_tool_json_error(req, before));
                submitted.push_back(req);
                continue;
            }
        }
        try {
            futures.push_back(pool_->submit(
                [reg_ptr, req, saved_ns]() -> ToolCallResult {
                    workflow::NamespaceGuard ns_guard(saved_ns);
                    ToolCallResult result;
                    result.tool_call_id = req.id;
                    result.name = req.name;
                    auto exec =
                        reg_ptr->execute(req.name, req.arguments);
                    result.success = exec.success && !output_is_structured_failure(exec.output);
                    result.output =
                        exec.success
                            ? exec.output
                            : container::String("Error: ") + exec.error;
                    return result;
                }));
            future_requests.push_back(req);
            submitted.push_back(req);
        } catch (const std::exception& e) {
            log::error_fmt("tool parallel submit failed: name={}, error={}", req.name, e.what());
        }
    }

    std::vector<ToolCallResult> results;
    results.reserve(requests.size());
    for (auto& result : immediate_results) results.push_back(std::move(result));
    for (size_t i = 0; i < futures.size(); ++i) {
        auto& f = futures[i];
        const auto& request = future_requests[i];
        auto status = f.wait_for(get_tool_timeout(request.name));
        if (status == std::future_status::timeout) {
            log::error_fmt("tool parallel execution timeout: name={}, timeout={}ms",
                           request.name, get_tool_timeout(request.name).count());
            results.push_back({request.id, request.name,
                               container::String("Error: Tool execution timeout"), false});
            continue;
        }
        try {
            auto result = f.get();
            truncate_tool_output(result);
            results.push_back(std::move(result));
        } catch (const std::exception& e) {
            log::error_fmt("tool parallel execution failed: name={}, error={}", request.name, e.what());
            results.push_back(make_tool_error(request, e.what()));
        }
    }

    for (const auto& request : requests) {
        bool found = false;
        for (const auto& submitted_request : submitted) {
            if (submitted_request.id == request.id) {
                found = true;
                break;
            }
        }
        if (!found) {
            results.push_back(make_tool_error(request, "Tool queue is full"));
        }
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
