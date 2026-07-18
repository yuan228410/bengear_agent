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

SubAgentResult SubAgentRuntime::execute(net::EventLoop& loop,
                                        const SubAgentTask& task,
                                        const config::SubAgentConfig& config) {
    SubAgentResult result;
    result.task_id = task.id;
    auto start = std::chrono::steady_clock::now();

    if (event_sink_) event_sink_->on_sub_agent_start(task.id, task.prompt);

    try {
        llm::ConversationHistory history;
        if (context_builder_) {
            history.set_system_prompt(context_builder_->build_with(
                memory::PromptSection::sub_agent,
                memory::PromptMode::sub_agent));
        } else if (!task.system_prompt.empty()) {
            history.set_system_prompt(task.system_prompt);
        } else {
            history.set_system_prompt(
                "You are a sub-agent. Answer concisely with only the essential information.");
        }
        history.add_user(task.prompt);

        if (event_sink_) event_sink_->on_sub_agent_progress(task.id, "calling LLM");

        auto response = net::sync_wait(loop,
            provider_.chat_with_tools_async(loop, history, tools_, {}, {}));

        std::string output_text;
        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];
            if (msg.contains("content") && !msg["content"].is_null()) {
                output_text = Json(msg["content"]).get<std::string>();
            } else if (msg.contains("tool_calls")) {
                output_text = "(sub-agent issued tool calls)";
                if (msg["tool_calls"].is_array()) {
                    result.tool_calls = static_cast<int>(msg["tool_calls"].size());
                }
            }
        }

        result.full_output = output_text;
        result.output = output_text;

        if (config.auto_summary && static_cast<int>(output_text.size()) > config.max_output_chars) {
            result.output = output_text.substr(0, static_cast<size_t>(config.max_output_chars))
                          + "\n...[truncated]";
            result.was_truncated = true;
        }

        result.success = true;
        result.status = SubAgentStatus::success;

        if (event_sink_) event_sink_->on_sub_agent_complete(task.id, result.output);

    } catch (const std::exception& e) {
        result.success = false;
        result.status = SubAgentStatus::failed;
        result.error = std::string(e.what());
        result.output = std::string("sub_agent error: ") + e.what();
        if (event_sink_) event_sink_->on_sub_agent_error(task.id, e.what());
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

std::vector<SubAgentResult> SubAgentRuntime::execute_parallel(
    net::EventLoop& loop,
    const std::vector<SubAgentTask>& tasks,
    const config::SubAgentConfig& config,
    int max_parallel) {
    if (tasks.empty()) return {};
    if (max_parallel <= 0) max_parallel = 1;

    std::vector<SubAgentResult> results(tasks.size());
    std::atomic<size_t> next{0};

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= tasks.size()) break;
            results[i] = execute(loop, tasks[i], config);
        }
    };

    int workers = std::min(max_parallel, static_cast<int>(tasks.size()));
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    return results;
}

} // namespace ben_gear::agent::runtime
