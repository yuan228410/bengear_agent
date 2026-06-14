#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/llm/run_outcome.hpp"
#include "ben_gear/llm/usage.hpp"

#include <utility>

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;

struct ChatRequest {
    container::String system_prompt;
    container::String user_prompt;
};

struct ChatResult {
    int status = 0;
    container::String text;
    container::String raw;
    container::String error_message;
    TokenUsage usage;        ///< API 返回的 token 用量
    RequestLatency latency;  ///< 请求延迟（含 TTFB）
    bool is_context_overflow = false;  ///< 上下文超限标记
    RunOutcome outcome = RunOutcome::success();

    bool ok() const noexcept {
        return status >= 200 && status < 300 && outcome.ok();
    }

    bool success() const noexcept { return ok(); }

    /// 构造错误结果（无 usage/latency）
    static ChatResult error(int code, container::String msg) {
        auto message = msg;
        return {.status = code, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = RunOutcome::provider_error(code, std::move(message))};
    }

    static ChatResult invalid_input(container::String msg) {
        auto message = msg;
        return {.status = 400, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = RunOutcome::invalid_input(std::move(message))};
    }

    static ChatResult context_overflow(container::String msg) {
        auto message = msg;
        return {.status = 400, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}, .is_context_overflow = true,
                .outcome = RunOutcome::context_overflow(std::move(message))};
    }

    static ChatResult tool_limit(int max_steps,
                                 int steps_used = -1,
                                 int max_tool_calls = 0,
                                 int tool_calls_used = 0,
                                 int max_tool_calls_per_step = 0,
                                 int tool_calls_in_step = 0,
                                 container::String message = container::String("Tool call limit reached")) {
        auto outcome = RunOutcome::tool_limit(max_steps, steps_used, max_tool_calls, tool_calls_used,
                                              max_tool_calls_per_step, tool_calls_in_step, std::move(message));
        auto error = outcome.message;
        return {.status = 409, .text = {}, .raw = {}, .error_message = std::move(error), .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = std::move(outcome)};
    }

    static ChatResult cancelled(container::String msg = container::String("Cancelled")) {
        auto message = msg;
        return {.status = 499, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = RunOutcome::cancelled(std::move(message))};
    }

    static ChatResult internal_error(container::String msg) {
        auto message = msg;
        return {.status = 500, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = RunOutcome::internal_error(std::move(message))};
    }

    /// 构造成功结果（无 usage/latency）
    static ChatResult ok(container::String text, container::String raw = {}) {
        return {.status = 200, .text = std::move(text), .raw = std::move(raw), .error_message = {}, .usage = {}, .latency = {}, .is_context_overflow = false,
                .outcome = RunOutcome::success()};
    }
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ChatRequest = llm::ChatRequest;
using ChatResult = llm::ChatResult;
}  // namespace ben_gear
