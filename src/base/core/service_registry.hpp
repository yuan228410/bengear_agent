#pragma once

#include <cassert>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace ben_gear::base {

/// 类型安全的服务注册表
///
/// 通过 type_index 实现 O(1) 查找，支持三种所有权模式：
///   - unique_ptr 注册（register_service<T>(unique_ptr)） — 取得所有权
///   - shared_ptr 注册（register_shared<T>(shared_ptr)）  — 共享所有权
///   - 裸指针注册  （register_service<T>(ptr)）           — 调用方保证生命周期
/// 用于解耦 Runtime 对具体服务的直接依赖，支持测试 Mock 注入。
///
/// 用法:
///   registry.register_service<IFileService>(std::make_unique<FileServiceImpl>());
///   registry.register_shared(shared_memory_store);
///   auto* svc = registry.resolve<IFileService>();
class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    /// 注册服务（取得所有权）
    template<typename Interface, typename Impl>
    bool register_service(std::unique_ptr<Impl> service) {
        static_assert(std::is_base_of_v<Interface, Impl>,
                      "Impl must derive from Interface");
        auto key = std::type_index(typeid(Interface));
        auto holder = std::make_unique<ServiceHolderImpl<Impl>>(std::move(service));
        entries_[key] = std::move(holder);
        return true;
    }

    /// 注册服务（shared_ptr 共享所有权）
    template<typename Interface>
    bool register_shared(std::shared_ptr<Interface> service) {
        auto key = std::type_index(typeid(Interface));
        entries_[key] = std::make_unique<ServiceHolderShared<Interface>>(std::move(service));
        return true;
    }

    /// 注册服务（不取得所有权，用于已存在的单例）
    template<typename Interface>
    bool register_service(Interface* service) {
        auto key = std::type_index(typeid(Interface));
        auto holder = std::make_unique<ServiceHolderPtr<Interface>>(service);
        entries_[key] = std::move(holder);
        return true;
    }

    /// 解析服务（返回 nullptr 表示未注册）
    template<typename Interface>
    Interface* resolve() const noexcept {
        auto it = entries_.find(std::type_index(typeid(Interface)));
        if (it == entries_.end()) return nullptr;
        return static_cast<Interface*>(it->second->ptr());
    }

    /// 解析服务引用（断言非空，用于必选服务）
    template<typename Interface>
    Interface& resolve_ref() const noexcept {
        auto* ptr = resolve<Interface>();
        assert(ptr && "ServiceRegistry::resolve_ref<T>() — service not registered");
        return *ptr;
    }

    /// 解析 shared_ptr 服务（仅对 register_shared 注册的服务有效）
    /// 返回空 shared_ptr 表示未注册或非共享所有权
    template<typename Interface>
    std::shared_ptr<Interface> resolve_shared() const noexcept {
        auto it = entries_.find(std::type_index(typeid(Interface)));
        if (it == entries_.end()) return nullptr;
        auto* shared_holder = dynamic_cast<ServiceHolderShared<Interface>*>(it->second.get());
        return shared_holder ? shared_holder->service_ : nullptr;
    }

    /// 是否已注册
    template<typename Interface>
    bool has() const noexcept {
        return entries_.count(std::type_index(typeid(Interface))) > 0;
    }

    /// 移除服务
    template<typename Interface>
    void remove() noexcept {
        entries_.erase(std::type_index(typeid(Interface)));
    }

    /// 清空所有服务
    void clear() noexcept { entries_.clear(); }

    /// 服务数量
    size_t size() const noexcept { return entries_.size(); }

private:
    struct ServiceHolderBase {
        virtual ~ServiceHolderBase() = default;
        virtual void* ptr() = 0;
    };

    template<typename Impl>
    struct ServiceHolderImpl : ServiceHolderBase {
        explicit ServiceHolderImpl(std::unique_ptr<Impl> svc)
            : service_(std::move(svc)) {}
        void* ptr() override { return service_.get(); }
        std::unique_ptr<Impl> service_;
    };

    template<typename Interface>
    struct ServiceHolderPtr : ServiceHolderBase {
        explicit ServiceHolderPtr(Interface* svc) : service_(svc) {}
        void* ptr() override { return service_; }
        Interface* service_;
    };

    template<typename Interface>
    struct ServiceHolderShared : ServiceHolderBase {
        explicit ServiceHolderShared(std::shared_ptr<Interface> svc)
            : service_(std::move(svc)) {}
        void* ptr() override { return service_.get(); }
        std::shared_ptr<Interface> service_;
    };

    std::unordered_map<std::type_index, std::unique_ptr<ServiceHolderBase>> entries_;
};

} // namespace ben_gear::base
