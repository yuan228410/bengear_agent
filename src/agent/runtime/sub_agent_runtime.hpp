#pragma once

#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <atomic>

#include "config/settings.hpp"
#include "net/event_loop.hpp"
#include "llm/provider_client.hpp"
#include "capabilities/tool/registry.hpp"
#include "domain/event.hpp"
#include "config/sub_agent_config.hpp"
#include "agent/sub_agent_types.hpp"
#include "base/core/event_bus.hpp"
#include "agent/core/events.hpp"

namespace ben_gear::memory { class ContextBuilder; }

namespace ben_gear::agent::runtime {

/// 子 Agent 运行时 — 支持流式进度推送
class SubAgentRuntime {
public:
    explicit SubAgentRuntime(const config::Settings& settings,
                             llm::ProviderClient& provider,
                             const capabilities::tool::ToolRegistry& tools);

    ~SubAgentRuntime();

    /// 设置提示词构建器（可选）
    void set_context_builder(memory::ContextBuilder* builder) { context_builder_ = builder; }

    /// 设置事件总线（用于推送流式进度）
    void set_event_bus(base::EventBus* bus) { event_bus_ = bus; }

    /// 执行单个子 Agent 任务（使用内部 EventLoop）
    SubAgentResult execute(const SubAgentTask& task,
                           const config::SubAgentConfig& config);

    /// 并行执行多个子 Agent 任务（使用内部 EventLoop）
    std::vector<SubAgentResult> execute_parallel(
        const std::vector<SubAgentTask>& tasks,
        const config::SubAgentConfig& config,
        int max_parallel);

    /// 默认配置
    const config::SubAgentConfig& default_config() const { return default_config_; }

    net::EventLoop& loop() noexcept {
        start_loop();
        return sub_loop_;
    }

private:
    void emit_progress(const std::string& task_id, const std::string& info);
    void emit_complete(const std::string& task_id, const std::string& summary);
    void emit_error(const std::string& task_id, const std::string& error);

    const config::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
    std::mutex provider_mutex_;
    memory::ContextBuilder* context_builder_ = nullptr;
    base::EventBus* event_bus_ = nullptr;

    void start_loop();
    void stop_loop();
    net::EventLoop sub_loop_;
    std::thread loop_thread_;
    std::mutex loop_mutex_;
    bool loop_running_ = false;
};

} // namespace ben_gear::agent::runtime
