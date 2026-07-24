#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ben_gear::base {

/// RAII 订阅令牌 — 析构时自动取消订阅
class Subscription {
public:
    Subscription() = default;  // 空订阅，无操作
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

/// 类型安全的事件总线 — 发布/订阅模式
///
/// 用法:
///   EventBus bus;
///   auto sub = bus.subscribe<TokenEvent>([](const TokenEvent& e) {
///       std::cout << e.token;
///   });
///   bus.publish(TokenEvent{"hello"});
///
/// 事件类型可以是任意普通 struct，按类型索引分发。
/// 订阅 RAII 管理：Subscription 销毁时自动取消订阅。
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// 注册 handler 异常回调（可选，默认静默忽略）
    /// type_name: 事件类型的 name()（如 "TokenEvent"）
    /// what: 异常消息
    /// 注意：回调在 publish() 内部调用，需避免再抛异常
    void set_error_handler(std::function<void(std::string_view type_name, std::string_view what)> h) {
        error_handler_ = std::move(h);
    }

    /// 订阅指定类型的事件
    /// Subscription 持有 shared_ptr，保证处理器在订阅期间存活
    template<typename E, typename Handler>
    Subscription subscribe(Handler&& handler) {
        auto key = std::type_index(typeid(E));
        auto wrapper = std::make_shared<HandlerWrapper<E>>(std::forward<Handler>(handler));
        
        std::lock_guard guard(mutex_);
        handlers_[key].push_back(wrapper);   // weak_ptr 从 shared_ptr 隐式构造
        
        auto keep_alive = std::weak_ptr<HandlerWrapperBase>(wrapper);
        return Subscription([this, key, keep_alive, wrapper = std::move(wrapper)]() {
            std::lock_guard lock(mutex_);
            auto it = handlers_.find(key);
            if (it == handlers_.end()) return;
            auto& vec = it->second;
            // 循环到末尾，确保移除所有匹配的 weak_ptr（同一 handler 可能注册多次）
            bool removed = false;
            for (auto i = vec.size(); i > 0; --i) {
                auto sp = vec[i - 1].lock();
                auto target = keep_alive.lock();
                if (!sp || (target && sp.get() == target.get())) {
                    vec.erase(vec.begin() + static_cast<ptrdiff_t>(i) - 1);
                    removed = true;
                    // 继续循环，不 break，确保同名 wrapper 全部移除
                }
            }
            (void)removed;
            if (vec.empty()) handlers_.erase(it);
        });
    }

    /// 发布事件 — 通知所有订阅者
    template<typename E>
    void publish(const E& event) {
        auto key = std::type_index(typeid(E));
        std::unique_lock lock(mutex_);
        auto it = handlers_.find(key);
        if (it == handlers_.end()) return;
        
        // 锁定有效订阅者避免迭代器失效
        std::vector<std::shared_ptr<HandlerWrapperBase>> snapshot;
        snapshot.reserve(it->second.size());
        for (auto& weak_h : it->second) {
            if (auto sp = weak_h.lock()) {
                snapshot.push_back(std::move(sp));
            }
        }
        if (snapshot.empty()) { handlers_.erase(it); return; }
        lock.unlock();
        
        for (auto& sp : snapshot) {
            auto* handler = static_cast<HandlerWrapper<E>*>(sp.get());
            try {
                handler->handler(event);
            } catch (const std::exception& e) {
                // 单个 handler 异常不中断其他订阅者
                // 仅记录，不传播（publish 不应改变调用方行为）
                on_handler_error(key.name(), e.what());
            } catch (...) {
                on_handler_error(key.name(), "unknown exception");
            }
        }
    }

    /// 检查某类型事件是否有订阅者
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

    /// 清空所有订阅
    void clear() {
        std::lock_guard lock(mutex_);
        handlers_.clear();
    }

private:
    /// 调用 error_handler_，若未注册则静默忽略
    void on_handler_error(std::string_view type_name, std::string_view what) {
        if (error_handler_) {
            error_handler_(type_name, what);
        }
    }

    struct HandlerWrapperBase {
        virtual ~HandlerWrapperBase() = default;
    };

    template<typename E>
    struct HandlerWrapper : HandlerWrapperBase {
        std::function<void(const E&)> handler;
        template<typename H>
        explicit HandlerWrapper(H&& h) : handler(std::forward<H>(h)) {}
    };

    mutable std::mutex mutex_;
    std::unordered_map<
        std::type_index,
        std::vector<std::weak_ptr<HandlerWrapperBase>>
    > handlers_;
    std::function<void(std::string_view, std::string_view)> error_handler_;
};

} // namespace ben_gear::base
