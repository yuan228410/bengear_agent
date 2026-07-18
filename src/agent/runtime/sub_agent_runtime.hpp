#pragma once

#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <atomic>

#include "base/config/settings.hpp"
#include "base/net/event_loop.hpp"
#include "llm/provider_client.hpp"
#include "capabilities/tool/registry.hpp"
#include "domain/event.hpp"
#include "base/config/sub_agent_config.hpp"
#include "agent/sub_agent_types.hpp"
#include "agent/core/event_sink.hpp"

namespace ben_gear::memory { class ContextBuilder; }

namespace ben_gear::agent::runtime {

class SubAgentRuntime {
public:
    explicit SubAgentRuntime(const config::Settings& settings,
                             llm::ProviderClient& provider,
                             const capabilities::tool::ToolRegistry& tools);

    ~SubAgentRuntime();

    void set_parent_event_sink(std::shared_ptr<domain::EventSink> sink) { parent_sink_ = std::move(sink); }

    /// 设置提示词构建器（可选）。设置后子 Agent 使用统一的 ContextBuilder 管道。
    void set_context_builder(memory::ContextBuilder* builder) { context_builder_ = builder; }

    SubAgentResult execute(net::EventLoop& loop,
                           const SubAgentTask& task,
                           const config::SubAgentConfig& config);

    std::vector<SubAgentResult> execute_parallel(
        net::EventLoop& loop,
        const std::vector<SubAgentTask>& tasks,
        const config::SubAgentConfig& config,
        int max_parallel);

    void set_event_sink(agent::SubAgentEventSink* sink) { event_sink_ = sink; }

    const config::SubAgentConfig& default_config() const { return default_config_; }

    net::EventLoop& loop() noexcept {
        start_loop();
        return sub_loop_;
    }

private:
    const config::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
    std::mutex provider_mutex_;
    memory::ContextBuilder* context_builder_ = nullptr;
    agent::SubAgentEventSink* event_sink_ = nullptr;

    void start_loop();
    void stop_loop();
    net::EventLoop sub_loop_;
    std::thread loop_thread_;
    std::mutex loop_mutex_;
    bool loop_running_ = false;
};

} // namespace ben_gear::agent::runtime
