#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

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

/// 同步重试
template <typename F>
auto with_retry(const config::Settings& settings, const char* operation, F&& f)
    -> decltype(f()) {
    auto& retry_config = settings.llm_request_retry;

    for (int attempt = 1; attempt <= retry_config.max_attempts; ++attempt) {
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
                log::warn_fmt("{} retryable status={} attempt={}/{} retry_in={}ms",
                              operation, result.status, attempt, retry_config.max_attempts,
                              retry_delay_ms(retry_config, attempt));
            } else {
                return result;
            }
        } catch (const std::exception& e) {
            if (attempt == retry_config.max_attempts) {
                log::error_fmt("{} exception after {} attempts: {}", operation, attempt, e.what());
                throw;
            }
            log::warn_fmt("{} exception attempt={}/{} retry_in={}ms: {}",
                          operation, attempt, retry_config.max_attempts,
                          retry_delay_ms(retry_config, attempt), e.what());
        }

        if (attempt < retry_config.max_attempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms(retry_config, attempt)));
        }
    }

    throw std::runtime_error(std::string(operation) + " max retry attempts exceeded");
}

}  // namespace ben_gear::llm
