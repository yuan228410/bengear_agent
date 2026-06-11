#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/llm/usage.hpp"

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

    /// 构造错误结果（无 usage/latency）
    static ChatResult error(int code, container::String msg) {
        return {.status = code, .text = {}, .raw = {}, .error_message = std::move(msg), .usage = {}, .latency = {}};
    }

    /// 构造成功结果（无 usage/latency）
    static ChatResult ok(container::String text, container::String raw = {}) {
        return {.status = 200, .text = std::move(text), .raw = std::move(raw), .error_message = {}, .usage = {}, .latency = {}};
    }
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ChatRequest = llm::ChatRequest;
using ChatResult = llm::ChatResult;
}  // namespace ben_gear
