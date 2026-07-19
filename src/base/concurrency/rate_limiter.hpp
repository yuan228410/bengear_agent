#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::base::concurrency {

/// 速率限制器接口 — 令牌桶算法
///
/// 职责：
/// - 控制请求速率，防止过载
/// - 支持突发流量（burst）
/// - 支持按资源名限制（如 per-model rate limiting）
class IRateLimiter {
public:
    virtual ~IRateLimiter() = default;

    /// 尝试获取一个令牌
    /// @return true 如果获取成功，false 如果需要等待
    virtual bool try_acquire() = 0;

    /// 等待直到可以获取令牌
    /// @param timeout 最大等待时间
    /// @return true 如果获取成功，false 如果超时
    virtual bool wait_for(std::chrono::milliseconds timeout) = 0;

    /// 获取剩余令牌数
    virtual int available_tokens() const = 0;

    /// 重置速率限制器
    virtual void reset() = 0;
};

/// 令牌桶速率限制器实现
class TokenBucketRateLimiter : public IRateLimiter {
public:
    /// @param rate 每秒产生的令牌数
    /// @param burst 最大突发大小（桶容量）
    TokenBucketRateLimiter(double rate, int burst);

    bool try_acquire() override;
    bool wait_for(std::chrono::milliseconds timeout) override;
    int available_tokens() const override;
    void reset() override;

private:
    void refill_tokens();

    double rate_;           // 每秒令牌数
    int burst_;             // 桶容量
    double tokens_;         // 当前令牌数
    std::chrono::steady_clock::time_point last_refill_;
    mutable std::mutex mutex_;
};

/// 按资源名的速率限制器（支持多资源独立限制）
class PerResourceRateLimiter {
public:
    /// @param rate 每秒产生的令牌数
    /// @param burst 最大突发大小
    PerResourceRateLimiter(double rate, int burst);

    /// 尝试获取指定资源的令牌
    bool try_acquire(const std::string& resource);

    /// 等待指定资源的令牌
    bool wait_for(const std::string& resource, std::chrono::milliseconds timeout);

    /// 获取指定资源的可用令牌数
    int available_tokens(const std::string& resource) const;

    /// 重置指定资源的速率限制器
    void reset(const std::string& resource);

    /// 重置所有资源的速率限制器
    void reset_all();

private:
    double rate_;
    int burst_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<TokenBucketRateLimiter>> limiters_;
};

} // namespace ben_gear::base::concurrency
