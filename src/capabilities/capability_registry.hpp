#pragma once

#include "capabilities/capability.hpp"
#include "workspace/types.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace ben_gear::capabilities {

/// Capability 注册表 — 管理所有 Capability 实例的创建与访问
/// 
/// 设计目标：
/// - 统一生命周期：通过 WorkspaceContext 创建所有 capability
/// - 单例模式：每个 capability 在注册表中唯一（按 name 索引）
/// - 线程安全：并发 get_or_create 安全
/// - 可扩展：新 capability 只需注册工厂函数
class CapabilityRegistry {
public:
    using Factory = std::function<std::unique_ptr<ICapability>(workspace::WorkspaceContext)>;

    static CapabilityRegistry& instance() {
        static CapabilityRegistry registry;
        return registry;
    }

    /// 注册 capability 工厂（程序启动时静态初始化）
    void register_factory(const char* name, Factory factory) {
        std::lock_guard lock(mutex_);
        factories_[name] = std::move(factory);
    }

    /// 获取或创建 capability（线程安全，延迟初始化）
    template <class T>
    T* get_or_create(const char* name, workspace::WorkspaceContext ws_ctx) {
        std::lock_guard lock(mutex_);
        auto it = instances_.find(name);
        if (it != instances_.end()) {
            return static_cast<T*>(it->second.get());
        }
        auto fit = factories_.find(name);
        if (fit == factories_.end()) {
            return nullptr; // 未注册
        }
        auto ptr = fit->second(std::move(ws_ctx));
        T* raw = static_cast<T*>(ptr.get());
        instances_[name] = std::move(ptr);
        return raw;
    }

    /// 检查 capability 是否已注册
    bool has_factory(const char* name) const {
        std::lock_guard lock(mutex_);
        return factories_.find(name) != factories_.end();
    }

    /// 清空（仅测试用）
    void clear() {
        std::lock_guard lock(mutex_);
        instances_.clear();
    }

private:
    CapabilityRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Factory> factories_;
    std::unordered_map<std::string, std::unique_ptr<ICapability>> instances_;
};

/// 静态注册辅助
class CapabilityRegistrar {
public:
    CapabilityRegistrar(const char* name, CapabilityRegistry::Factory factory) {
        CapabilityRegistry::instance().register_factory(name, std::move(factory));
    }
};

#define BEN_GEAR_REGISTER_CAPABILITY(name, Type) \
    namespace { \
        ben_gear::capabilities::CapabilityRegistrar registrar_##Type( \
            name, \
            [](ben_gear::workspace::WorkspaceContext ws_ctx) { \
                return std::make_unique<Type>(std::move(ws_ctx)); \
            }); \
    }

} // namespace ben_gear::capabilities