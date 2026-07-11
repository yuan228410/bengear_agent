#pragma once

#include "workspace/types.hpp"

namespace ben_gear::capabilities {

/// 所有 Capability 的基类接口
class ICapability {
public:
    virtual ~ICapability() = default;

    /// Capability 名称（用于注册表查找）
    virtual const char* name() const noexcept = 0;

    /// 初始化（在注册表创建实例后调用，可用于依赖其他 capability）
    virtual void init() {}
};

/// CRTP 基类：自动实现 name() 并提供 WorkspaceContext 访问
template <class Derived>
class CapabilityBase : public ICapability {
public:
    explicit CapabilityBase(workspace::WorkspaceContext ws_ctx)
        : ws_ctx_(std::move(ws_ctx)) {}

    const char* name() const noexcept override { return Derived::kName; }

    const workspace::WorkspaceContext& workspace_context() const noexcept {
        return ws_ctx_;
    }

protected:
    const workspace::WorkspaceContext ws_ctx_;
};

/// 编译期 capability 标识
template <class T>
struct CapabilityTraits {
    static constexpr const char* name = T::kName;
};

} // namespace ben_gear::capabilities