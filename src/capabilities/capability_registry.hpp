#pragma once

#include "capabilities/capability.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ben_gear::capabilities {

/// Capability 工厂类型
using CapabilityFactory = std::function<std::unique_ptr<ICapability>(workspace::WorkspaceContext)>;

/// 全局 Capability 注册表 — 单例
/// 参照 llm::ProviderRegistry 的静态 registrar 模式
class CapabilityRegistry {
public:
    static CapabilityRegistry& instance() {
        static CapabilityRegistry registry;
        return registry;
    }

    /// 注册 Capability 工厂（线程安全，启动阶段调用）
    void register_capability(const char* name, CapabilityFactory factory) {
        std::lock_guard lock(mutex_);
        factories_[name] = std::move(factory);
    }

    /// 创建所有已注册的 Capability 实例
    std::vector<std::unique_ptr<ICapability>> create_all(workspace::WorkspaceContext ws_ctx) const {
        std::lock_guard lock(mutex_);
        std::vector<std::unique_ptr<ICapability>> instances;
        instances.reserve(factories_.size());
        for (const auto& [name, factory] : factories_) {
            if (auto inst = factory(ws_ctx)) instances.push_back(std::move(inst));
        }
        return instances;
    }

    /// 按名称创建单个 Capability
    std::unique_ptr<ICapability> create(const char* name, workspace::WorkspaceContext ws_ctx) const {
        std::lock_guard lock(mutex_);
        auto it = factories_.find(name);
        if (it == factories_.end()) return nullptr;
        return it->second(ws_ctx);
    }

    /// 检查是否已注册
    bool has(const char* name) const {
        std::lock_guard lock(mutex_);
        return factories_.find(name) != factories_.end();
    }

    /// 清空注册表（仅测试用）
    void clear() {
        std::lock_guard lock(mutex_);
        factories_.clear();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return factories_.size();
    }

private:
    CapabilityRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CapabilityFactory, std::hash<std::string>, std::equal_to<>> factories_;
};

/// 静态注册器 — 程序启动时自动注册
class CapabilityRegistrar {
public:
    CapabilityRegistrar(const char* name, CapabilityFactory factory) {
        CapabilityRegistry::instance().register_capability(name, std::move(factory));
    }
};

} // namespace ben_gear::capabilities

/// 注册 Capability 宏（在 .cpp 文件中使用，自动执行静态注册）
#define BEN_GEAR_REGISTER_CAPABILITY(CapabilityClass)                                              \
    namespace {                                                                                     \
        ::ben_gear::capabilities::CapabilityRegistrar BEN_GEAR_CONCAT(registrar_, CapabilityClass)( \
            CapabilityClass::kName,                                                                 \
            [](::ben_gear::workspace::WorkspaceContext ws_ctx) -> std::unique_ptr<::ben_gear::capabilities::ICapability> { \
                return std::make_unique<CapabilityClass>(std::move(ws_ctx));                         \
            });                                                                                     \
    }

#define BEN_GEAR_CONCAT_IMPL(a, b) a##b
#define BEN_GEAR_CONCAT(a, b) BEN_GEAR_CONCAT_IMPL(a, b)
