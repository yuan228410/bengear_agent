#pragma once

#include "ben_gear/llm/usage.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::llm {

/// 从 OpenAI 响应 JSON 提取 token 用量
/// OpenAI 格式: {"usage": {"prompt_tokens": N, "completion_tokens": N, "total_tokens": N,
///                           "prompt_tokens_details": {"cached_tokens": N}}}
inline TokenUsage extract_openai_usage(const Json& response) {
    TokenUsage usage;
    if (!response.contains("usage") || !response["usage"].is_object()) {
        return usage;
    }
    auto u = response["usage"];
    usage.prompt_tokens = u.value("prompt_tokens", 0);
    usage.completion_tokens = u.value("completion_tokens", 0);
    usage.total_tokens = u.value("total_tokens", 0);

    // 提取缓存 token（OpenAI 新 API）
    if (u.contains("prompt_tokens_details") && u["prompt_tokens_details"].is_object()) {
        auto details = u["prompt_tokens_details"];
        usage.cached_tokens = details.value("cached_tokens", 0);
    }

    return usage;
}

/// 从 Anthropic 响应 JSON 提取 token 用量
/// Anthropic 格式: {"usage": {"input_tokens": N, "output_tokens": N,
///                            "cache_creation_input_tokens": N, "cache_read_input_tokens": N}}
inline TokenUsage extract_anthropic_usage(const Json& response) {
    TokenUsage usage;
    if (!response.contains("usage") || !response["usage"].is_object()) {
        return usage;
    }
    auto u = response["usage"];
    usage.prompt_tokens = u.value("input_tokens", 0);
    usage.completion_tokens = u.value("output_tokens", 0);
    usage.total_tokens = usage.prompt_tokens + usage.completion_tokens;

    // Anthropic 缓存读取 token
    usage.cached_tokens = u.value("cache_read_input_tokens", 0);

    return usage;
}

/// 从 OpenAI 流式 SSE 事件提取 usage（最后一个 chunk 可能包含 usage）
inline TokenUsage extract_openai_stream_usage(const Json& chunk) {
    return extract_openai_usage(chunk);
}

/// 从 Anthropic 流式事件提取 usage
/// message_start 事件包含 input_tokens，message_delta 包含 output_tokens
inline TokenUsage extract_anthropic_stream_usage(const Json& event_data) {
    return extract_anthropic_usage(event_data);
}

/// 根据响应格式自动提取（尝试 OpenAI 再尝试 Anthropic）
inline TokenUsage extract_usage_auto(const Json& response) {
    auto usage = extract_openai_usage(response);
    if (!usage.empty()) return usage;
    return extract_anthropic_usage(response);
}

} // namespace ben_gear::llm
