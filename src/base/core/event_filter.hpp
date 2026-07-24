#pragma once

#include "base/core/event_bus.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::base {

/// 事件过滤器接口 — 决定是否将事件传递给下游
///
/// 用法：
///   EventBus bus;
///   auto filter = EventFilter::from_config({.include_tokens=true, .include_thinking=false});
///   bus.subscribe<agent::TokenEvent>([&](const auto& e) { /* only if filter allows */ });
///
/// 默认实现 EventFilter::Default 包含全部事件。
/// 可继承重写 should_forward() 实现自定义过滤策略。
class EventFilter {
public:
    struct Config {
        bool include_tokens = true;
        bool include_thinking = true;
        bool include_tool_calls = true;
        bool include_stats = true;
        bool include_exec_events = true;
        bool include_todo = true;
        bool include_sub_agent = true;
    };

    virtual ~EventFilter() = default;
    EventFilter() = default;
    EventFilter(const EventFilter&) = default;
    EventFilter& operator=(const EventFilter&) = default;

    /// 从配置构造默认过滤器
    explicit EventFilter(Config config) : config_(config) {}

    /// 获取当前配置
    Config current_config() const { return config_; }

    /// 更新配置
    void update_config(Config config) { config_ = std::move(config); }

    /// 检查是否应转发指定事件类型
    virtual bool should_forward(std::type_index event_type) const;

    /// 根据配置创建过滤器实例
    static std::unique_ptr<EventFilter> from_config(Config config);

private:
    Config config_ = {};
};

/// 通用的条件订阅适配器 — 将过滤器逻辑嵌入现有订阅
///
/// 将一个受 EventFilter 控制的条件 handler 附加到 EventBus 上。
/// 当 should_forward() 返回 false 时，handler 不会被触发。
class FilteredSubscription {
public:
    /// 构造条件订阅：filter 控制是否转发到 handler
    template<typename E>
    static Subscription bind(EventBus& bus,
                             const EventFilter& filter,
                             std::function<void(const E&)> handler) {
        if (!filter.should_forward(std::type_index(typeid(E)))) {
            return {};  // 空订阅，不过滤
        }
        return bus.subscribe<E>(std::move(handler));
    }
};

} // namespace ben_gear::base