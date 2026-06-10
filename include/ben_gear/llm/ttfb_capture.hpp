#pragma once

#include "ben_gear/llm/usage.hpp"

#include <atomic>
#include <chrono>
#include <functional>

namespace ben_gear::llm {

/// TTFB（Time To First Byte）捕获器
///
/// 包装流式 on_token 回调，在首个 token 到达时记录时间点，
/// 配合请求起始时间计算 TTFB 和总延迟。
///
/// 用法：
///   TtfbCapture ttfb;
///   handlers.on_token = ttfb.wrap(std::move(handlers.on_token));
///   // ... co_await request ...
///   auto latency = ttfb.build_latency(start);
///
/// 线程安全：captured_ 用 atomic，可在多线程回调中使用
/// 独立文件：避免 usage.hpp ↔ stream.hpp 循环依赖
class TtfbCapture {
public:
    TtfbCapture() = default;

    /// 包装原始 on_token 回调，首次 token 时记录 TTFB
    /// 返回 std::function<void(std::string_view)> 与 StreamHandlers::on_token 兼容
    std::function<void(std::string_view)> wrap(
        std::function<void(std::string_view)> orig) {
        return [this, orig = std::move(orig)](std::string_view token) {
            if (!captured_.exchange(true)) {
                ttfb_time_ = std::chrono::steady_clock::now();
            }
            if (orig) orig(token);
        };
    }

    /// 根据请求起始时间构建 RequestLatency（含 TTFB）
    RequestLatency build_latency(std::chrono::steady_clock::time_point start) const {
        RequestLatency latency;
        latency.total_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        if (captured_) {
            latency.ttfb_seconds = std::chrono::duration<double>(
                ttfb_time_ - start).count();
            latency.has_ttfb = true;
        }
        return latency;
    }

    bool has_ttfb() const { return captured_; }

private:
    std::atomic<bool> captured_{false};
    std::chrono::steady_clock::time_point ttfb_time_;
};

} // namespace ben_gear::llm

namespace ben_gear {
using TtfbCapture = llm::TtfbCapture;
} // namespace ben_gear
