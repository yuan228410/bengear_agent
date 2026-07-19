#pragma once

#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "base/core/service_registry.hpp"
#include "base/core/event_bus.hpp"
#include "base/core/metrics.hpp"
#include "base/core/tracing.hpp"
#include "base/config/settings.hpp"
#include "base/utils/json.hpp"
#include "domain/errors.hpp"
#include "base/net/event_loop.hpp"
#include "base/net/cancel.hpp"
#include "base/net/task.hpp"

#include "workspace/types.hpp"
#include "workspace/session.hpp"
#include "agent/runtime/lifecycle_manager.hpp"
#include "llm/chat.hpp"

namespace ben_gear::agent::runtime {

class RuntimeFactory;

/// Agent 运行时 — 轻量级服务编排器
///
/// Runtime 持有全部子服务的实例，通过 ServiceRegistry 统一管理。
/// 外部代码通过 services().resolve<T>() 获取服务，无直接 accessor。
/// Runtime 本身只暴露高层次 API：run_session_async、make_session、shutdown。
class Runtime : public std::enable_shared_from_this<Runtime> {
    friend class RuntimeFactory;
public:
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// 服务注册表（获取所有子服务的统一入口）
    base::ServiceRegistry& services() noexcept { return services_; }
    const base::ServiceRegistry& services() const noexcept { return services_; }

    /// 优雅关闭全部服务
    void shutdown();

    /// 生命周期状态
    LifecycleManager& lifecycle() noexcept { return lifecycle_; }
    const LifecycleManager& lifecycle() const noexcept { return lifecycle_; }

    /// 在会话上运行 Agent 循环
    struct SessionRunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        net::CancellationToken cancel;
    };

    net::Task<llm::ChatResult> run_session_async(SessionRunConfig config);
    /// 创建会话
    std::unique_ptr<workspace::Session> make_session(std::string session_id = {});

    /// Agent 运行配置
    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }
    int max_parallel_tools() const noexcept { return max_parallel_tools_; }

private:
    Runtime(config::Settings settings,
            workspace::WorkspaceContext ws_ctx);

    // ─── 注册服务到 ServiceRegistry ──────────────────────────────
    void register_services();

    // ─── 成员 ─────────────────────────────────────────────────────
    base::ServiceRegistry services_;
    config::Settings settings_;
    workspace::WorkspaceContext ws_ctx_;
    LifecycleManager lifecycle_;
    base::EventBus event_bus_;
    base::NoopMetricsCollector metrics_;
    base::NoopTracer tracer_;

    // Agent 配置缓存
    int max_tool_steps_ = 0;
    int max_tool_calls_ = 0;
    int max_tool_calls_per_step_ = 0;
    int max_parallel_tools_ = 0;

    // 以下成员仅由 RuntimeFactory 初始化，通过 ServiceRegistry 访问
    struct InternalServices;
    std::unique_ptr<InternalServices> internal_;
};

} // namespace ben_gear::agent::runtime
