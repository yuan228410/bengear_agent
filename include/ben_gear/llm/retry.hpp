#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/http.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace ben_gear::llm {

inline bool is_retryable_status(int status) {
    return status == 429 || status >= 500;
}

/// 计算指数退避延迟（毫秒），供同步/异步重试共用
inline int retry_delay_ms(const config::LlmRequestRetrySettings& cfg, int attempt) {
    auto delay = cfg.initial_delay_ms * (1 << (attempt - 1));
    if (delay > cfg.max_delay_ms) delay = cfg.max_delay_ms;
    return delay;
}

/// 从响应头解析 Retry-After，返回毫秒数（解析失败返回 0）
/// Retry-After 可以是秒数或 HTTP 日期，此处仅处理秒数
inline int parse_retry_after_ms(const net::HttpResponse& response) {
    if (!response.status || response.status != 429) return 0;
    auto it = response.headers.find("retry-after");
    if (it == response.headers.end()) return 0;
    try {
        auto seconds = std::stoi(it->second);
        if (seconds > 0) return seconds * 1000;
    } catch (...) {}
    return 0;
}

/// 同步重试
template <typename F>
auto with_retry(const config::Settings& settings, const char* operation, F&& f)
    -> decltype(f()) {
    auto& retry_config = settings.llm_request_retry;

    for (int attempt = 1; attempt <= retry_config.max_attempts; ++attempt) {
        int delay = 0;
        try {
            auto result = f();

            if constexpr (requires { result.status; }) {
                if (result.status >= 200 && result.status < 300) {
                    if (attempt > 1) {
                        log::info_fmt("{} succeeded on attempt={}", operation, attempt);
                    }
                    return result;
                }
                if (!is_retryable_status(result.status) || attempt == retry_config.max_attempts) {
                    log::error_fmt("{} failed status={} attempt={}/{}", operation, result.status, attempt, retry_config.max_attempts);
                    return result;
                }
                delay = retry_delay_ms(retry_config, attempt);
                if constexpr (requires { result.headers; }) {
                    auto retry_after = parse_retry_after_ms(result);
                    if (retry_after > 0) delay = std::max(delay, retry_after);
                }
                log::warn_fmt("{} retryable status={} attempt={}/{} retry_in={}ms",
                              operation, result.status, attempt, retry_config.max_attempts, delay);
            } else {
                return result;
            }
        } catch (const std::exception& e) {
            if (attempt == retry_config.max_attempts) {
                log::error_fmt("{} exception after {} attempts: {}", operation, attempt, e.what());
                throw;
            }
            delay = retry_delay_ms(retry_config, attempt);
            log::warn_fmt("{} exception attempt={}/{} retry_in={}ms: {}",
                          operation, attempt, retry_config.max_attempts, delay, e.what());
        }

        if (attempt < retry_config.max_attempts && delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }

    throw std::runtime_error(std::string(operation) + " max retry attempts exceeded");
}

/// 异步重试（协程版本）
/// F 是返回 Task<T> 的协程函数，T 需要有 .status 字段
template <typename F>
auto with_retry_async(net::EventLoop& loop,
                       const config::Settings& settings,
                       const char* operation,
                       F&& f,
                       const net::CancellationToken& cancel = {}) -> decltype(f()) {
    using ResultType = typename decltype(f())::value_type;
    auto& retry_config = settings.llm_request_retry;

    for (int attempt = 1; attempt <= retry_config.max_attempts; ++attempt) {
        cancel.throw_if_cancelled();
        auto result = co_await f();

        if (result.status >= 200 && result.status < 300) {
            if (attempt > 1) {
                log::info_fmt("{} succeeded on attempt={}", operation, attempt);
            }
            co_return result;
        }

        bool retryable = is_retryable_status(result.status);
        if (!retryable || attempt == retry_config.max_attempts) {
            log::error_fmt("{} failed status={} attempt={}/{}",
                           operation, result.status, attempt, retry_config.max_attempts);
            co_return result;
        }

        auto delay = retry_delay_ms(retry_config, attempt);
        if constexpr (requires { result.headers; }) {
            auto retry_after = parse_retry_after_ms(result);
            if (retry_after > 0) delay = std::max(delay, retry_after);
        }
        log::warn_fmt("{} retryable status={} attempt={}/{} retry_in={}ms",
                       operation, result.status, attempt, retry_config.max_attempts, delay);
        co_await loop.sleep_for(std::chrono::milliseconds(delay));
    }

    co_return ResultType{};
}

/// 异步 HTTP 重试：重试原始 HTTP 请求，成功后应用 transform
/// F_http 返回 Task<Resp>（Resp 需有 .status 字段），F_transform 接受 Resp&& 返回 T
template <typename F_http, typename F_transform>
auto with_http_retry_async(net::EventLoop& loop,
                            const config::Settings& settings,
                            const char* operation,
                            F_http&& http_fn,
                            F_transform&& transform,
                            const net::CancellationToken& cancel = {}) -> net::Task<std::decay_t<decltype(transform(std::declval<typename std::decay_t<decltype(http_fn())>::value_type>()))>> {
    using Resp = typename std::decay_t<decltype(http_fn())>::value_type;
    using T = std::decay_t<decltype(transform(std::declval<Resp>()))>;
    auto& retry_config = settings.llm_request_retry;

    for (int attempt = 1; attempt <= retry_config.max_attempts; ++attempt) {
        cancel.throw_if_cancelled();
        auto response = co_await http_fn();

        if (response.status >= 200 && response.status < 300) {
            if (attempt > 1) {
                log::info_fmt("{} succeeded on attempt={}", operation, attempt);
            }
            co_return transform(std::move(response));
        }

        bool retryable = is_retryable_status(response.status);
        if (!retryable || attempt == retry_config.max_attempts) {
            log::error_fmt("{} failed status={} attempt={}/{}",
                           operation, response.status, attempt, retry_config.max_attempts);
            co_return transform(std::move(response));
        }

        auto delay = retry_delay_ms(retry_config, attempt);
        if constexpr (requires { response.headers; }) {
            auto retry_after = parse_retry_after_ms(response);
            if (retry_after > 0) delay = std::max(delay, retry_after);
        }
        log::warn_fmt("{} retryable status={} attempt={}/{} retry_in={}ms",
                       operation, response.status, attempt, retry_config.max_attempts, delay);
        co_await loop.sleep_for(std::chrono::milliseconds(delay));
    }

    co_return T{};
}

}  // namespace ben_gear::llm
