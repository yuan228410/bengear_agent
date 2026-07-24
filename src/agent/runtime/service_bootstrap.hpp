#pragma once

#include "base/core/service_registry.hpp"
#include "base/core/event_bus.hpp"
#include "base/core/metrics.hpp"
#include "base/core/tracing.hpp"
#include "config/settings.hpp"
#include "workspace/types.hpp"
#include "agent/runtime/lifecycle_manager.hpp"

#include <memory>
#include <string>

namespace ben_gear::workspace { class Session; }

namespace ben_gear::agent::runtime {

struct MemoryContext;
struct InfrastructureServices;

/// 服务引导器 — 持有全部子服务实例，负责注册/关闭
///
/// 从 Runtime 拆分而来，消除上帝对象。
/// Runtime 持有 ServiceBootstrap，通过它管理服务生命周期。
class ServiceBootstrap {
public:
    ServiceBootstrap(config::Settings settings, workspace::WorkspaceContext ws_ctx);
    ~ServiceBootstrap();

    ServiceBootstrap(const ServiceBootstrap&) = delete;
    ServiceBootstrap& operator=(const ServiceBootstrap&) = delete;

    /// 创建 InternalServices 并注册服务到 ServiceRegistry
    void init();

    /// 逆序关闭全部服务
    void shutdown();

    /// 服务注册表
    base::ServiceRegistry& services() noexcept { return services_; }
    const base::ServiceRegistry& services() const noexcept { return services_; }

    /// 生命周期
    LifecycleManager& lifecycle() noexcept { return lifecycle_; }
    const LifecycleManager& lifecycle() const noexcept { return lifecycle_; }

    /// 事件总线
    base::EventBus& event_bus() noexcept { return event_bus_; }

    /// 可观测性
    base::NoopMetricsCollector& metrics() noexcept { return metrics_; }
    base::NoopTracer& tracer() noexcept { return tracer_; }

    /// 配置
    const config::Settings& settings() const noexcept { return settings_; }
    const workspace::WorkspaceContext& ws_ctx() const noexcept { return ws_ctx_; }

    /// 获取可变 MemoryContext（供 RuntimeFactory 初始化阶段注入依赖）
    /// 初始化完成后不应再调用
    MemoryContext& memory() noexcept;

    // Agent 配置缓存
    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }
    int max_parallel_tools() const noexcept { return max_parallel_tools_; }

    /// 创建会话
    std::unique_ptr<workspace::Session> make_session(std::string session_id = "");

private:
    void register_services();

    base::ServiceRegistry services_;
    config::Settings settings_;
    workspace::WorkspaceContext ws_ctx_;
    LifecycleManager lifecycle_;
    base::EventBus event_bus_;
    base::NoopMetricsCollector metrics_;
    base::NoopTracer tracer_;

    int max_tool_steps_ = 0;
    int max_tool_calls_ = 0;
    int max_tool_calls_per_step_ = 0;
    int max_parallel_tools_ = 0;

    struct InternalServices;
    std::unique_ptr<InternalServices> internal_;
};

} // namespace ben_gear::agent::runtime
