#pragma once

#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <mutex>

namespace ben_gear::llm {

/// 单次请求 token 用量（从 API 响应 usage 字段提取）
struct TokenUsage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    int cached_tokens = 0;     // OpenAI cached_prompt_tokens

    bool empty() const { return total_tokens == 0 && completion_tokens == 0; }
};

/// 单次请求延迟
struct RequestLatency {
    double total_seconds = 0.0;    // 请求总耗时
    double ttfb_seconds = 0.0;     // 首 token 延迟（流式有效）
    bool has_ttfb = false;         // 是否有 TTFB 数据
};

/// 累计用量统计（线程安全）
///
/// 采集层写入，暴露层读取，UI 层只读展示
/// 三层解耦：采集 → Tracker → Session → 任意 UI
///
/// 核心能力：记录 API 实际 prompt_tokens，用于压缩判断校准
/// 压缩判断不再纯估算，而是 last_actual_prompt_tokens + 增量估算
class UsageTracker {
public:
    /// 记录一次请求的用量和延迟
    void record(const TokenUsage& usage, const RequestLatency& latency) {
        std::lock_guard lock(mutex_);
        request_count_++;
        total_prompt_tokens_ += usage.prompt_tokens;
        total_completion_tokens_ += usage.completion_tokens;
        total_tokens_ += usage.total_tokens;
        total_cached_tokens_ += usage.cached_tokens;
        total_latency_seconds_ += latency.total_seconds;

        last_usage_ = usage;
        last_latency_ = latency;

        // 记录实际 prompt_tokens，用于下次压缩判断校准
        if (usage.prompt_tokens > 0) {
            last_actual_prompt_tokens_ = usage.prompt_tokens;
        }

        log::info_fmt("usage: request #{}, prompt={}, completion={}, cached={}, total={}, latency={:.2f}s{}",
            request_count_, usage.prompt_tokens, usage.completion_tokens,
            usage.cached_tokens, usage.total_tokens, latency.total_seconds,
            latency.has_ttfb ? ", ttfb=" + std::to_string(latency.ttfb_seconds).substr(0, 5) + "s" : "");
    }

    // ---- 读取接口（UI 层调用）----

    int request_count() const {
        std::lock_guard lock(mutex_);
        return request_count_;
    }
    int total_prompt_tokens() const {
        std::lock_guard lock(mutex_);
        return total_prompt_tokens_;
    }
    int total_completion_tokens() const {
        std::lock_guard lock(mutex_);
        return total_completion_tokens_;
    }
    int total_tokens() const {
        std::lock_guard lock(mutex_);
        return total_tokens_;
    }
    int total_cached_tokens() const {
        std::lock_guard lock(mutex_);
        return total_cached_tokens_;
    }
    double total_latency_seconds() const {
        std::lock_guard lock(mutex_);
        return total_latency_seconds_;
    }
    double avg_latency_seconds() const {
        std::lock_guard lock(mutex_);
        return request_count_ > 0 ? total_latency_seconds_ / request_count_ : 0.0;
    }
    TokenUsage last_usage() const {
        std::lock_guard lock(mutex_);
        return last_usage_;
    }
    RequestLatency last_latency() const {
        std::lock_guard lock(mutex_);
        return last_latency_;
    }

    /// 上次 API 返回的实际 prompt_tokens（用于压缩判断校准）
    /// 返回 0 表示尚无实际数据，需要纯估算
    int last_actual_prompt_tokens() const {
        std::lock_guard lock(mutex_);
        return last_actual_prompt_tokens_;
    }

    /// 基于实际值的上下文占用估算（用于压缩判断）
    /// actual + estimated_increment，比纯估算精确得多
    int64_t estimated_context_usage(int64_t estimated_increment = 0) const {
        std::lock_guard lock(mutex_);
        if (last_actual_prompt_tokens_ > 0) {
            // 有实际值：基于上次实际 + 增量估算
            return static_cast<int64_t>(last_actual_prompt_tokens_) + estimated_increment;
        }
        // 无实际值：返回 0，调用方应回退到纯估算
        return 0;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        request_count_ = 0;
        total_prompt_tokens_ = 0;
        total_completion_tokens_ = 0;
        total_tokens_ = 0;
        total_cached_tokens_ = 0;
        total_latency_seconds_ = 0.0;
        last_actual_prompt_tokens_ = 0;
        last_usage_ = {};
        last_latency_ = {};
    }

private:
    mutable std::mutex mutex_;
    int request_count_ = 0;
    int total_prompt_tokens_ = 0;
    int total_completion_tokens_ = 0;
    int total_tokens_ = 0;
    int total_cached_tokens_ = 0;
    double total_latency_seconds_ = 0.0;
    int last_actual_prompt_tokens_ = 0;  // API 实际值，校准压缩判断
    TokenUsage last_usage_;
    RequestLatency last_latency_;
};

} // namespace ben_gear::llm

namespace ben_gear {
using TokenUsage = llm::TokenUsage;
using RequestLatency = llm::RequestLatency;
using UsageTracker = llm::UsageTracker;
} // namespace ben_gear
