#pragma once

#include <memory>
#include <string>

#include "base/core/service_registry.hpp"
#include "base/core/event_bus.hpp"
#include "base/core/metrics.hpp"
#include "base/core/tracing.hpp"
#include "config/settings.hpp"
#include "net/cancel.hpp"
#include "net/task.hpp"

#include "workspace/types.hpp"
#include "agent/runtime/lifecycle_manager.hpp"
#include "llm/chat.hpp"

// 前向声明 — 减少下游编译依赖
namespace ben_gear::net { class EventLoop; }
namespace ben_gear::workspace { class Session; }

namespace ben_gear::agent::runtime {

struct MemoryContext;
class RuntimeFactory;
class RuntimeBuilder;

/// Agent 运行时 — 轻量级服务编排器
///
/// Runtime 持有全部子服务的实例，通过 ServiceRegistry 统一管理。
/// 外部代码通过 services().resolve<T>() 获取服务，无直接 accessor。
/// Runtime 本身只暴露高层次 API：run_session_async、make_session、shutdown。
class Runtime : public std::enable_shared_from_this<Runtime> {
    friend class RuntimeFactory;
    friend class RuntimeBuilder;
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

    /// 工厂方法 — 封装 new，避免外部直接调用 new Runtime
    static std::unique_ptr<Runtime> make(config::Settings settings,
                                         workspace::WorkspaceContext ws_ctx) {
        return std::unique_ptr<Runtime>(new Runtime(std::move(settings), std::move(ws_ctx)));
    }

    // ─── 注册服务到 ServiceRegistry ──────────────────────────────
    void register_services();
    void init_internals();

    // 友元工厂通过此访问器直接获取 MemoryContext（避免 dynamic_cast）
    MemoryContext& mutable_memory() noexcept;

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

/// Runtime 构建器 — 支持在初始化前预注入 Mock 服务
///
/// 用法：
///   auto rt = RuntimeBuilder(settings, ws_ctx)
///       .with(mock_file_svc)
///       .with(mock_web_svc)
///       .build();
///   RuntimeFactory::initialize(*rt);
class RuntimeBuilder {
public:
    RuntimeBuilder(config::Settings settings, workspace::WorkspaceContext ws_ctx)
        : settings_(std::move(settings)), ws_ctx_(std::move(ws_ctx)) {}

    /// 预注册一个服务实例（在 InternalServices 创建前注入）
    template <typename T>
    RuntimeBuilder& with(T& service) {
        pre_regs_.emplace_back([&service](base::ServiceRegistry& r) {
            r.register_service<T>(&service);
        });
        return *this;
    }

    /// 构建 Runtime 实例（不调用 initialize，仅构造 + 预注入）
    std::unique_ptr<Runtime> build() {
        auto rt = Runtime::make(std::move(settings_), std::move(ws_ctx_));
        for (auto& reg : pre_regs_) {
            reg(rt->services());
        }
        rt->init_internals();
        return rt;
    }

private:
    config::Settings settings_;
    workspace::WorkspaceContext ws_ctx_;
    std::vector<std::function<void(base::ServiceRegistry&)>> pre_regs_;
};

} // namespace ben_gear::agent::runtime
