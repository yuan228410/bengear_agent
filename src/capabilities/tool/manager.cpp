#include "capabilities/tool/manager.hpp"
#include "base/log/logger.hpp"

#include <string>
#include <string_view>
#include <unordered_set>

namespace ben_gear::capabilities::tool {

namespace {
constexpr size_t kMaxToolOutputChars = 200000;

ToolCallResult make_tool_error(const ToolCallRequest& request, std::string_view message) {
    ToolCallResult result;
    result.tool_call_id = request.id;
    result.name = request.name;
    result.output = std::string("Error: ");
    result.output.append(message);
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

bool output_is_structured_failure(const std::string& output) {
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

void ToolCallManager::set_tool_timeout(
    const std::string& tool_name,
    std::chrono::milliseconds timeout) {
    tool_timeouts_[tool_name] = timeout;
}

std::chrono::milliseconds ToolCallManager::get_tool_timeout(
    const std::string& tool_name) const {
    auto it = tool_timeouts_.find(tool_name);
    return it != tool_timeouts_.end() ? it->second : timeout_;
}


ToolCallResult ToolCallManager::execute_tool(
    const ToolCallRequest& request) const {
    const auto saved_ns = namespace_;
    const auto* reg_ptr = &registry_;
    std::future<ToolCallResult> future;
    try {
        future = pool_->submit(
            [reg_ptr, request, saved_ns]() -> ToolCallResult {
                ToolCallResult result;
                result.tool_call_id = request.id;
                result.name = request.name;
                auto exec = reg_ptr->execute(request.name, request.arguments);
                result.success = exec.success && !output_is_structured_failure(exec.output);
                result.output =
                    exec.success
                        ? exec.output
                        : std::string("Error: ") + exec.error;
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
                std::string("Error: Tool execution timeout"), false};
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

    const auto saved_ns = namespace_;
    const auto* reg_ptr = &registry_;
    std::vector<ToolCallRequest> submitted;
    submitted.reserve(requests.size());
    for (auto req : requests) {
        try {
            futures.push_back(pool_->submit(
                [reg_ptr, req, saved_ns]() -> ToolCallResult {
                    ToolCallResult result;
                    result.tool_call_id = req.id;
                    result.name = req.name;
                    auto exec =
                        reg_ptr->execute(req.name, req.arguments);
                    result.success = exec.success && !output_is_structured_failure(exec.output);
                    result.output =
                        exec.success
                            ? exec.output
                            : std::string("Error: ") + exec.error;
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
                               std::string("Error: Tool execution timeout"), false});
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

    std::unordered_set<std::string> submitted_ids;
    for (const auto& submitted_request : submitted) {
        submitted_ids.insert(submitted_request.id);
    }
    for (const auto& request : requests) {
        if (submitted_ids.find(request.id) == submitted_ids.end()) {
            results.push_back(make_tool_error(request, "Tool queue is full"));
        }
    }

    return results;
}
}  // namespace ben_gear::capabilities::tool
