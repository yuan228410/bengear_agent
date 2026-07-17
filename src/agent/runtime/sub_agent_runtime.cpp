#include "agent/runtime/sub_agent_runtime.hpp"

#include "llm/conversation_history.hpp"
#include "memory/context.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <vector>

namespace ben_gear::agent::runtime {

using Json = ben_gear::Json;

SubAgentRuntime::SubAgentRuntime(

    const config::Settings& settings,
    llm::ProviderClient& provider,
    const capabilities::tool::ToolRegistry& tools)
    : default_config_(settings.agent.sub_agent),
      settings_(settings),
      provider_(provider),
      tools_(tools) {}

SubAgentRuntime::~SubAgentRuntime() {
    stop_loop();
}

void SubAgentRuntime::start_loop() {
    std::lock_guard lock(loop_mutex_);
    if (loop_running_) return;
    loop_running_ = true;
    sub_loop_.reset_stop();
    loop_thread_ = std::thread([this] { sub_loop_.run(); });
}

void SubAgentRuntime::stop_loop() {
    std::lock_guard lock(loop_mutex_);
    if (!loop_running_) return;
    loop_running_ = false;
    sub_loop_.stop();
    if (loop_thread_.joinable()) loop_thread_.join();
}

void SubAgentRuntime::execute_locked(
    net::EventLoop& loop, std::string_view prompt,
    const config::SubAgentConfig& config, Result& result) {
    std::lock_guard lock(provider_mutex_);
    result = execute(loop, prompt, config);
}

SubAgentRuntime::Result
SubAgentRuntime::execute(net::EventLoop& loop,
                         std::string_view prompt,
                         const config::SubAgentConfig& config) {
    Result result;
    auto start = std::chrono::steady_clock::now();

    try {
        llm::ConversationHistory history;
        if (context_builder_) {
            history.set_system_prompt(context_builder_->build_with(
                memory::PromptSection::sub_agent,
                memory::PromptMode::sub_agent));
        } else {
            history.set_system_prompt(
                "You are a sub-agent. Answer concisely with only the essential information.");
        }
        history.add_user(prompt);

        auto response = net::sync_wait(loop,
            provider_.chat_with_tools_async(loop, history, tools_, {}, {}));

        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];
            if (msg.contains("content") && !msg["content"].is_null()) {
                auto text = Json(msg["content"]).get<std::string>();
                result.output = text;
            } else if (msg.contains("tool_calls")) {
                result.output = "(sub-agent issued tool calls)";
            }
        }

        if (config.auto_summary && static_cast<int>(result.output.size()) > config.max_output_chars) {
            result.output = result.output.substr(0, static_cast<size_t>(config.max_output_chars))
                          + "\n...[truncated]";
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.output = std::string("sub_agent error: ") + e.what();
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

std::vector<SubAgentRuntime::Result>
SubAgentRuntime::execute_parallel(
    net::EventLoop& loop,
    const std::vector<std::string>& prompts,
    const config::SubAgentConfig& config,
    int max_parallel) {

    if (prompts.empty()) return {};
    if (max_parallel <= 0) max_parallel = 1;

    std::vector<Result> results(prompts.size());
    std::atomic<size_t> next{0};

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= prompts.size()) break;
            execute_locked(loop, prompts[i], config, results[i]);
        }
    };

    int workers = std::min(max_parallel, static_cast<int>(prompts.size()));
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    return results;
}

} // namespace ben_gear::agent::runtime
