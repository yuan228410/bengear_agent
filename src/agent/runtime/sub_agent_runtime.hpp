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

namespace ben_gear::agent::runtime {

class SubAgentRuntime {
public:
    explicit SubAgentRuntime(const config::Settings& settings,
                             llm::ProviderClient& provider,
                             const capabilities::tool::ToolRegistry& tools);

    ~SubAgentRuntime();

    void set_parent_event_sink(std::shared_ptr<domain::EventSink> sink) { parent_sink_ = std::move(sink); }

    struct Result {
        bool success = false;
        std::string output;
        int tool_calls = 0;
        std::chrono::milliseconds duration{0};
    };

    Result execute(net::EventLoop& loop,
                   std::string_view prompt,
                   const config::SubAgentConfig& config);

    std::vector<Result> execute_parallel(
        net::EventLoop& loop,
        const std::vector<std::string>& prompts,
        const config::SubAgentConfig& config,
        int max_parallel);

    const config::SubAgentConfig& default_config() const { return default_config_; }

    net::EventLoop& loop() noexcept {
        start_loop();
        return sub_loop_;
    }

private:
    void execute_locked(net::EventLoop& loop, std::string_view prompt,
                        const config::SubAgentConfig& config, Result& result);
    const config::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
    std::mutex provider_mutex_;

    void start_loop();
    void stop_loop();
    net::EventLoop sub_loop_;
    std::thread loop_thread_;
    std::mutex loop_mutex_;
    bool loop_running_ = false;
};

} // namespace ben_gear::agent::runtime
