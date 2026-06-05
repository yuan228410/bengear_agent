#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>

namespace ben_gear::net {

/// 请求取消时抛出的异常
struct OperationCancelled : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// 协作式取消令牌，可跨协程/线程共享
class CancellationToken {
public:
    CancellationToken() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() const { cancelled_->store(true, std::memory_order_release); }
    bool is_cancelled() const { return cancelled_->load(std::memory_order_acquire); }
    void throw_if_cancelled() const {
        if (is_cancelled()) throw OperationCancelled("request cancelled");
    }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

}  // namespace ben_gear::net

namespace ben_gear {
using CancellationToken = net::CancellationToken;
using OperationCancelled = net::OperationCancelled;
}  // namespace ben_gear
