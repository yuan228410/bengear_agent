#pragma once

#include "base/core/service_registry.hpp"
#include "config/settings.hpp"
#include "net/cancel.hpp"
#include "net/task.hpp"
#include "llm/chat.hpp"

#include <memory>
#include <string>

namespace ben_gear::net { class EventLoop; }
namespace ben_gear::workspace { class Session; }

namespace ben_gear::agent::runtime {

/// 会话执行器 — 从 Runtime 拆分而来
///
/// 负责组装执行循环（ExecutionLoop + 拦截器链）并运行 Agent 会话。
/// 通过 ServiceRegistry 获取所需服务，不持有状态。
class SessionRunner {
public:
    struct RunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        std::string agent_type;  ///< 内置 agent 类型（"build"/"plan"/""=默认react）
        net::CancellationToken cancel;
    };

    /// 运行 Agent 会话
    /// @param services 服务注册表
    /// @param config 会话配置
    /// @param max_tool_steps 最大工具步数
    /// @param max_tool_calls 最大工具调用数
    /// @param max_parallel_tools 最大并行工具数
    static net::Task<llm::ChatResult> run(
        base::ServiceRegistry& services,
        RunConfig config,
        int max_tool_steps,
        int max_tool_calls,
        int max_parallel_tools);
};

} // namespace ben_gear::agent::runtime
