#pragma once

#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace ben_gear::base {

/// 类型安全的服务注册表
///
/// 通过 type_index 实现 O(1) 查找，支持 unique_ptr 所有权转移和裸指针注册。
/// 用于解耦 Runtime 对具体服务的直接依赖，支持测试 Mock 注入。
///
/// 用法:
///   registry.register_service<IFileService>(std::make_unique<FileServiceImpl>());
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

    std::unordered_map<std::type_index, std::unique_ptr<ServiceHolderBase>> entries_;
};

} // namespace ben_gear::base
