#pragma once

#include "llm/run_outcome.hpp"
#include "llm/usage.hpp"

#include <utility>

namespace ben_gear::llm {

// 使用命名空间别名简化代码

struct ChatRequest {
    std::string system_prompt;
    std::string user_prompt;
};

struct ChatResult {
    int status = 0;
    std::string text = {};
    std::string raw = {};
    std::string error_message = {};
    TokenUsage usage = {};
    RequestLatency latency = {};
    bool is_context_overflow = false;
    RunOutcome outcome = RunOutcome::success();

    bool ok() const noexcept {
        return status >= 200 && status < 300 && outcome.ok();
    }

    bool success() const noexcept { return ok(); }

    /// 构造错误结果（无 usage/latency）
    static ChatResult error(int code, std::string msg) {
        auto outcome = RunOutcome::provider_error(code, std::string(msg));
        return {.status = code, .text = {}, .raw = {},
                .error_message = std::move(msg), .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = std::move(outcome)};
    }

    static ChatResult invalid_input(std::string msg) {
        auto outcome = RunOutcome::invalid_input(std::string(msg));
        return {.status = 400, .text = {}, .raw = {},
                .error_message = std::move(msg), .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = std::move(outcome)};
    }

    static ChatResult context_overflow(std::string msg) {
        auto outcome = RunOutcome::context_overflow(std::string(msg));
        return {.status = 400, .text = {}, .raw = {},
                .error_message = std::move(msg), .usage = {}, .latency = {},
                .is_context_overflow = true, .outcome = std::move(outcome)};
    }

    static ChatResult tool_limit(int max_steps,
                                 int steps_used = -1,
                                 int max_tool_calls = 0,
                                 int tool_calls_used = 0,
                                 int max_tool_calls_per_step = 0,
                                 int tool_calls_in_step = 0,
                                 std::string message = std::string("Tool call limit reached")) {
        auto outcome = RunOutcome::tool_limit(max_steps, steps_used, max_tool_calls, tool_calls_used,
                                              max_tool_calls_per_step, tool_calls_in_step, std::move(message));
        return {.status = 409, .text = {}, .raw = {},
                .error_message = std::string(outcome.message), .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = std::move(outcome)};
    }

    static ChatResult cancelled(std::string msg = std::string("Cancelled")) {
        auto outcome = RunOutcome::cancelled(std::string(msg));
        return {.status = 499, .text = {}, .raw = {},
                .error_message = std::move(msg), .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = std::move(outcome)};
    }

    static ChatResult internal_error(std::string msg) {
        auto outcome = RunOutcome::internal_error(std::string(msg));
        return {.status = 500, .text = {}, .raw = {},
                .error_message = std::move(msg), .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = std::move(outcome)};
    }

    /// 构造成功结果（无 usage/latency）
    static ChatResult ok(std::string text, std::string raw = {}) {
        return {.status = 200, .text = std::move(text), .raw = std::move(raw),
                .error_message = {}, .usage = {}, .latency = {},
                .is_context_overflow = false, .outcome = RunOutcome::success()};
    }
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ChatRequest = llm::ChatRequest;
using ChatResult = llm::ChatResult;
}  // namespace ben_gear
