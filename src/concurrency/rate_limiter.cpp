#include "concurrency/rate_limiter.hpp"
#include <mutex>
#include <thread>
#include <chrono>

namespace ben_gear::base::concurrency {

// ==================== TokenBucketRateLimiter ====================

TokenBucketRateLimiter::TokenBucketRateLimiter(double rate, int burst)
    : rate_(rate), burst_(burst), tokens_(static_cast<double>(burst)),
      last_refill_(std::chrono::steady_clock::now()) {}

void TokenBucketRateLimiter::refill_tokens() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
    tokens_ = std::min(static_cast<double>(burst_),
                       tokens_ + elapsed * rate_);
    last_refill_ = now;
}

bool TokenBucketRateLimiter::try_acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    refill_tokens();
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }
    return false;
}

bool TokenBucketRateLimiter::wait_for(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (try_acquire()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        // 计算需要等待的时间
        std::lock_guard<std::mutex> lock(mutex_);
        refill_tokens();
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        double deficit = 1.0 - tokens_;
        auto wait_time = std::chrono::milliseconds(
            static_cast<int64_t>(deficit / rate_ * 1000));
        wait_time = std::min(wait_time,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - std::chrono::steady_clock::now()));
        if (wait_time.count() <= 0) {
            return false;
        }
        // 简单的自旋等待（对于短等待）或条件变量等待（对于长等待）
        std::this_thread::sleep_for(std::min(wait_time,
                                             std::chrono::milliseconds(10)));
    }
}

int TokenBucketRateLimiter::available_tokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const_cast<TokenBucketRateLimiter*>(this)->refill_tokens();
    return static_cast<int>(tokens_);
}

void TokenBucketRateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_ = static_cast<double>(burst_);
    last_refill_ = std::chrono::steady_clock::now();
}

// ==================== PerResourceRateLimiter ====================

PerResourceRateLimiter::PerResourceRateLimiter(double rate, int burst)
    : rate_(rate), burst_(burst) {}

bool PerResourceRateLimiter::try_acquire(const std::string& resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limiters_.find(resource);
    if (it == limiters_.end()) {
        auto limiter = std::make_unique<TokenBucketRateLimiter>(rate_, burst_);
        bool result = limiter->try_acquire();
        limiters_[resource] = std::move(limiter);
        return result;
    }
    return it->second->try_acquire();
}

bool PerResourceRateLimiter::wait_for(const std::string& resource,
                                      std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limiters_.find(resource);
    if (it == limiters_.end()) {
        auto limiter = std::make_unique<TokenBucketRateLimiter>(rate_, burst_);
        bool result = limiter->wait_for(timeout);
        limiters_[resource] = std::move(limiter);
        return result;
    }
    return it->second->wait_for(timeout);
}

int PerResourceRateLimiter::available_tokens(const std::string& resource) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limiters_.find(resource);
    if (it == limiters_.end()) {
        return burst_;
    }
    return it->second->available_tokens();
}

void PerResourceRateLimiter::reset(const std::string& resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limiters_.find(resource);
    if (it != limiters_.end()) {
        it->second->reset();
    }
}

void PerResourceRateLimiter::reset_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    limiters_.clear();
}

} // namespace ben_gear::base::concurrency
