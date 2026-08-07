#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ben_gear::base {

/// RAII 订阅令牌 — 析构时自动取消订阅
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> unsub)
        : unsubscribe_(std::move(unsub)) {}
    ~Subscription() {
        if (unsubscribe_) unsubscribe_();
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept
        : unsubscribe_(std::move(other.unsubscribe_)) {
        other.unsubscribe_ = nullptr;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            if (unsubscribe_) unsubscribe_();
            unsubscribe_ = std::move(other.unsubscribe_);
            other.unsubscribe_ = nullptr;
        }
        return *this;
    }

private:
    std::function<void()> unsubscribe_;
};

/// 类型安全的事件总线 — 发布/订阅模式 + 异步分发
///
/// 用法:
///   EventBus bus;
///   auto sub = bus.subscribe<TokenEvent>([](const TokenEvent& e) { ... });
///   bus.publish(TokenEvent{"hello"});        // 同步：当前线程分发
///   bus.publish_async(TokenEvent{"world"});   // 异步：后台线程分发，不阻塞
///
/// 订阅 RAII 管理，subscribe / clear 在初始化阶段单线程调用，
/// publish / publish_async 可在任意线程调用。
class EventBus {
    // ─── 内部类型（必须在 publish 之前定义）─────────────────────
    struct HandlerWrapperBase {
        virtual ~HandlerWrapperBase() = default;
    };

    template<typename E>
    struct HandlerWrapper : HandlerWrapperBase {
        std::function<void(const E&)> handler;
        template<typename H>
        explicit HandlerWrapper(H&& h) : handler(std::forward<H>(h)) {}
    };

    // ─── 内部方法（必须在 publish / publish_async 之前声明）─────
    template<typename E>
    std::vector<std::shared_ptr<HandlerWrapperBase>> snapshot_handlers() {
        auto key = std::type_index(typeid(E));
        std::unique_lock lock(mutex_);
        auto it = handlers_.find(key);
        if (it == handlers_.end()) return {};

        std::vector<std::shared_ptr<HandlerWrapperBase>> snapshot;
        snapshot.reserve(it->second.size());
        for (auto& weak_h : it->second) {
            if (auto sp = weak_h.lock()) {
                snapshot.push_back(std::move(sp));
            }
        }
        if (snapshot.empty()) handlers_.erase(it);
        return snapshot;
    }

    void on_handler_error(std::string_view type_name, std::string_view what) {
        if (error_handler_) {
            error_handler_(type_name, what);
        }
    }

    void ensure_worker() {
        // 析构中不允许创建新 worker
        if (shutting_down_.load(std::memory_order_acquire)) return;
        if (!worker_running_.load(std::memory_order_acquire)) {
            bool expected = false;
            if (worker_running_.compare_exchange_strong(expected, true)) {
                try {
                    worker_ = std::thread(&EventBus::worker_loop, this);
                } catch (...) {
                    // 线程创建失败时回滚 worker_running_，避免任务永久堆积
                    worker_running_.store(false, std::memory_order_release);
                }
            }
        }
    }

    void worker_loop() {
        while (worker_running_.load(std::memory_order_acquire)) {
            std::function<void()> task;
            {
                std::unique_lock lock(queue_mutex_);
                cv_.wait(lock, [this] {
                    return !queue_.empty() ||
                           !worker_running_.load(std::memory_order_acquire);
                });
                if (!worker_running_.load(std::memory_order_acquire) && queue_.empty())
                    break;
                if (queue_.empty()) continue;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            if (task) task();
        }
    }

public:
    EventBus() = default;
    ~EventBus() { shutdown_worker(); }
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    void set_error_handler(std::function<void(std::string_view, std::string_view)> h) {
        error_handler_ = std::move(h);
    }

    /// 订阅指定类型的事件
    template<typename E, typename Handler>
    Subscription subscribe(Handler&& handler) {
        auto key = std::type_index(typeid(E));
        auto wrapper = std::make_shared<HandlerWrapper<E>>(std::forward<Handler>(handler));

        std::lock_guard guard(mutex_);
        handlers_[key].push_back(wrapper);

        auto keep_alive = std::weak_ptr<HandlerWrapperBase>(wrapper);
        return Subscription([this, key, keep_alive, wrapper = std::move(wrapper)]() {
            std::lock_guard lock(mutex_);
            auto it = handlers_.find(key);
            if (it == handlers_.end()) return;
            auto& vec = it->second;
            // swap-and-pop：O(1) 移除，避免批量取消订阅时退化为 O(n²)
            for (size_t i = 0; i < vec.size(); ) {
                auto sp = vec[i].lock();
                auto target = keep_alive.lock();
                if (!sp || (target && sp.get() == target.get())) {
                    if (i < vec.size() - 1) std::swap(vec[i], vec.back());
                    vec.pop_back();
                } else {
                    ++i;
                }
            }
            if (vec.empty()) handlers_.erase(it);
        });
    }

    /// 同步发布 — 当前线程逐一调用所有订阅者
    template<typename E>
    void publish(const E& event) {
        auto snapshot = snapshot_handlers<E>();
        if (snapshot.empty()) return;

        for (auto& sp : snapshot) {
            auto* handler = static_cast<HandlerWrapper<E>*>(sp.get());
            try {
                handler->handler(event);
            } catch (const std::exception& e) {
                on_handler_error(typeid(E).name(), e.what());
            } catch (...) {
                on_handler_error(typeid(E).name(), "unknown exception");
            }
        }
    }

    /// 异步发布 — 入队后立即返回，后台线程分发
    ///
    /// 适用于 TokenEvent / ThinkingEvent 等高频流式事件。
    /// 订阅者的执行速度不影响发布者的吞吐。
    /// worker 线程在首次 publish_async 调用时惰性启动。
    template<typename E>
    void publish_async(const E& event) {
        ensure_worker();

        auto snapshot = snapshot_handlers<E>();
        if (snapshot.empty()) return;

        E copy = event;
        {
            std::lock_guard lock(queue_mutex_);
            queue_.push_back([snapshot = std::move(snapshot),
                              e = std::move(copy)]() {
                using EType = E;
                for (auto& sp : snapshot) {
                    auto* handler = static_cast<HandlerWrapper<EType>*>(sp.get());
                    try {
                        handler->handler(e);
                    } catch (...) {
                        // 异步分发中单个 handler 异常不影响其他订阅者
                    }
                }
            });
        }
        cv_.notify_one();
    }

    template<typename E>
    bool has_subscribers() const {
        std::lock_guard lock(mutex_);
        auto it = handlers_.find(std::type_index(typeid(E)));
        if (it == handlers_.end()) return false;
        for (auto& w : it->second) {
            if (!w.expired()) return true;
        }
        return false;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        handlers_.clear();
    }

    /// 关闭异步 worker 线程（析构时自动调用）
    void shutdown_worker() {
        // 先设置 shutting_down 阻止 ensure_worker 创建新线程
        shutting_down_.store(true, std::memory_order_release);
        bool expected = true;
        if (!worker_running_.compare_exchange_strong(expected, false)) return;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<
        std::type_index,
        std::vector<std::weak_ptr<HandlerWrapperBase>>
    > handlers_;
    std::function<void(std::string_view, std::string_view)> error_handler_;

    // 异步分发
    std::deque<std::function<void()>> queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> shutting_down_{false};
};

} // namespace ben_gear::base
