#pragma once

#include <memory>
#include <string>

#include "base/core/service_registry.hpp"
#include "base/core/event_bus.hpp"
#include "config/settings.hpp"
#include "net/cancel.hpp"
#include "net/task.hpp"

#include "workspace/types.hpp"
#include "agent/runtime/lifecycle_manager.hpp"
#include "agent/runtime/service_bootstrap.hpp"
#include "llm/chat.hpp"

// 前向声明 — 减少下游编译依赖
namespace ben_gear::net { class EventLoop; }
namespace ben_gear::workspace { class Session; }

namespace ben_gear::agent::runtime {

class RuntimeFactory;
class RuntimeBuilder;

/// Agent 运行时 — 组合根，持有 ServiceBootstrap 并转发调用
///
/// 从上帝对象拆分而来：
/// - ServiceBootstrap：服务注册、生命周期管理、会话创建
/// - SessionRunner：会话执行（静态方法，无状态）
/// - Runtime：组合根，对外暴露统一 API
class Runtime : public std::enable_shared_from_this<Runtime> {
public:
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// 服务注册表（获取所有子服务的统一入口）
    base::ServiceRegistry& services() noexcept { return bootstrap_.services(); }
    const base::ServiceRegistry& services() const noexcept { return bootstrap_.services(); }

    /// 优雅关闭全部服务
    void shutdown() { bootstrap_.shutdown(); }

    /// 初始化服务（创建 InternalServices + 注册）
    /// 由 RuntimeBuilder::build() 或 RuntimeFactory::initialize() 调用
    void init() { bootstrap_.init(); }

    /// 生命周期状态
    LifecycleManager& lifecycle() noexcept { return bootstrap_.lifecycle(); }
    const LifecycleManager& lifecycle() const noexcept { return bootstrap_.lifecycle(); }

    /// 在会话上运行 Agent 循环
    struct SessionRunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        std::string agent_type;  ///< 内置 agent 类型（"build"/"plan"/""）
        net::CancellationToken cancel;
    };

    net::Task<llm::ChatResult> run_session_async(SessionRunConfig config);

    /// 创建会话
    std::unique_ptr<workspace::Session> make_session(std::string session_id = "");

    /// Agent 运行配置
    int max_tool_steps() const noexcept { return bootstrap_.max_tool_steps(); }
    int max_tool_calls() const noexcept { return bootstrap_.max_tool_calls(); }
    int max_tool_calls_per_step() const noexcept { return bootstrap_.max_tool_calls_per_step(); }
    int max_parallel_tools() const noexcept { return bootstrap_.max_parallel_tools(); }

    /// ServiceBootstrap 访问（供 RuntimeFactory 初始化阶段使用）
    ServiceBootstrap& bootstrap() noexcept { return bootstrap_; }

private:
    Runtime(config::Settings settings,
            workspace::WorkspaceContext ws_ctx);

    /// 工厂方法 — 封装 new，避免外部直接调用 new Runtime
    static std::unique_ptr<Runtime> make(config::Settings settings,
                                         workspace::WorkspaceContext ws_ctx) {
        return std::unique_ptr<Runtime>(new Runtime(std::move(settings), std::move(ws_ctx)));
    }

    friend class RuntimeBuilder;
    friend class RuntimeFactory;

    ServiceBootstrap bootstrap_;
};

/// Runtime 构建器 — 支持在初始化前预注入 Mock 服务
class RuntimeBuilder {
public:
    RuntimeBuilder(config::Settings settings, workspace::WorkspaceContext ws_ctx)
        : settings_(std::move(settings)), ws_ctx_(std::move(ws_ctx)) {}

    template <typename T>
    RuntimeBuilder& with(T& service) {
        pre_regs_.emplace_back([&service](base::ServiceRegistry& r) {
            r.register_service<T>(&service);
        });
        return *this;
    }

    std::unique_ptr<Runtime> build() {
        auto rt = Runtime::make(std::move(settings_), std::move(ws_ctx_));
        for (auto& reg : pre_regs_) {
            reg(rt->services());
        }
        rt->init();
        return rt;
    }

private:
    config::Settings settings_;
    workspace::WorkspaceContext ws_ctx_;
    std::vector<std::function<void(base::ServiceRegistry&)>> pre_regs_;
};

} // namespace ben_gear::agent::runtime
