#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace ben_gear::agent::runtime {

/// 生命周期阶段
enum class LifecyclePhase {
    Created,        // 对象已创建
    Initializing,   // 正在初始化
    Ready,          // 初始化完成，可以使用
    ShuttingDown,   // 正在关闭
    Destroyed       // 已销毁
};

/// 生命周期事件类型
enum class LifecycleEvent {
    BeforeInit,     // 初始化前
    AfterInit,      // 初始化后
    BeforeShutdown, // 关闭前
    AfterShutdown   // 关闭后
};

/// 生命周期观察者接口
///
/// 实现此接口以监听 Runtime 的生命周期事件。
class ILifecycleObserver {
public:
    virtual ~ILifecycleObserver() = default;

    /// 处理生命周期事件
    virtual void on_lifecycle_event(LifecycleEvent event,
                                    const std::string& component_name = "") = 0;
};

/// 生命周期管理器
///
/// 管理 Runtime 的生命周期状态和事件通知。
/// 确保子系统按正确顺序初始化和关闭。
class LifecycleManager {
public:
    LifecycleManager() : phase_(LifecyclePhase::Created) {}

    /// 注册生命周期观察者
    void add_observer(std::shared_ptr<ILifecycleObserver> observer);

    /// 移除生命周期观察者
    void remove_observer(ILifecycleObserver* observer);

    /// 获取当前生命周期阶段
    LifecyclePhase phase() const noexcept { return phase_; }

    /// 检查是否可以接受请求
    bool is_ready() const noexcept { return phase_ == LifecyclePhase::Ready; }

    /// 进入初始化阶段
    void begin_initialization(const std::string& component_name = "");

    /// 初始化完成
    void end_initialization(const std::string& component_name = "");

    /// 进入关闭阶段
    void begin_shutdown(const std::string& component_name = "");

    /// 关闭完成
    void end_shutdown(const std::string& component_name = "");

    /// 强制设置阶段（仅用于测试或特殊场景）
    void force_phase(LifecyclePhase phase);

private:
    void notify_observers(LifecycleEvent event, const std::string& component_name);

    LifecyclePhase phase_;
    std::vector<std::shared_ptr<ILifecycleObserver>> observers_;
};

} // namespace ben_gear::agent::runtime
