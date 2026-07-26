#include "agent/runtime/sub_agent_runtime.hpp"
#include <mutex>
#include <thread>
#include <chrono>

#include "llm/conversation_history.hpp"
#include "acp/core/message.hpp"

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
      tools_(tools),
      sub_agent_tools_(tools.without(default_config_.exclude_tools)) {}

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

SubAgentResult SubAgentRuntime::execute(const SubAgentTask& task,
                                        const config::SubAgentConfig& config) {
    start_loop();

    SubAgentResult result;
    result.task_id = task.id;
    auto start = std::chrono::steady_clock::now();

    emit_progress(task.id, "started");

    try {
        // ─── 子 Agent 无系统提示词，所有上下文由主 Agent 在 task prompt 中提供 ───
        llm::ConversationHistory history;
        history.set_system_prompt(std::string{});

        log::debug_fmt("sub_agent tools: {} available", sub_agent_tools_->size());
        history.add_user(task.prompt);

        // 如果 task.tool_filter 指定了白名单，进一步过滤工具
        const capabilities::tool::ToolRegistry* active_tools = sub_agent_tools_.get();
        std::unique_ptr<capabilities::tool::ToolRegistry> filtered_tools;
        if (!task.tool_filter.empty()) {
            std::vector<std::string> excluded;
            for (const auto& name : sub_agent_tools_->tool_names()) {
                if (std::find(task.tool_filter.begin(), task.tool_filter.end(), name) == task.tool_filter.end()) {
                    excluded.push_back(name);
                }
            }
            filtered_tools = sub_agent_tools_->without(excluded);
            active_tools = filtered_tools.get();
            log::debug_fmt("sub_agent: tool_filter applied, tools={}/{}",
                filtered_tools->size(), sub_agent_tools_->size());
        }

        int max_steps = config.default_max_steps > 0 ? config.default_max_steps : 10;
        int tool_call_count = 0;
        std::string final_output;
        bool timed_out = false;

        // ─── ReAct 循环 ──────────────────────────────────────────────
        for (int step = 0; step < max_steps; ++step) {
            emit_progress(task.id, "step " + std::to_string(step + 1) + "/" + std::to_string(max_steps));

            // 超时检查
            if (task.timeout.count() > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                if (elapsed > task.timeout) {
                    final_output = "(sub-agent timed out)";
                    timed_out = true;
                    break;
                }
            }

            // 调用 LLM
            auto response = net::sync_wait(sub_loop_,
                provider_.chat_with_tools_async(sub_loop_, history, *active_tools, {}, {},
                    config.model_override));

            log::debug_fmt("sub_agent: LLM step {} done, tokens={}",
                step, response.value("usage", Json()).value("total_tokens", 0));

            // ─── 解析响应 ────────────────────────────────────────────
            std::vector<acp::ToolCallRequest> tool_calls;
            std::string text_content;
            bool is_openai = response.contains("choices") && response["choices"].is_array();
            bool is_anthropic = !is_openai && response.contains("content") && response["content"].is_array();

            if (is_openai) {
                // OpenAI 格式（用值拷贝避免 ProxyRef 中间临时对象悬空）
                auto choices = response["choices"];
                if (!choices.empty()) {
                    auto msg = choices[0]["message"];
                    if (msg.contains("content") && !msg["content"].is_null()) {
                        text_content = msg["content"].get<std::string>();
                    }
                    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                        auto tcs = msg["tool_calls"];
                        for (auto& tc : tcs) {
                            acp::ToolCallRequest req;
                            req.id = tc["id"].get<std::string>();
                            req.name = tc["function"]["name"].get<std::string>();
                            auto args_str = tc["function"]["arguments"].get<std::string>();
                            req.arguments = Json::parse(args_str);
                            tool_calls.push_back(std::move(req));
                        }
                    }
                }
            } else if (is_anthropic) {
                // Anthropic 格式（值拷贝避免 ProxyRef 悬空）
                auto content_blocks = response["content"];
                for (auto& block : content_blocks) {
                    auto type = block.value("type", "");
                    if (type == "text") {
                        text_content += block.value("text", "");
                    } else if (type == "tool_use") {
                        acp::ToolCallRequest req;
                        req.id = block["id"].get<std::string>();
                        req.name = block["name"].get<std::string>();
                        req.arguments = block["input"];
                        tool_calls.push_back(std::move(req));
                    }
                }
            } else {
                // 未知格式，尝试当作文本
                text_content = response.dump();
            }

            // ─── 没有工具调用 → 最终答案 ──────────────────────────
            if (tool_calls.empty()) {
                final_output = text_content;
                break;
            }

            // ─── 有工具调用 → 将 assistant 消息加入历史 ───────────
            {
                auto asst_msg = acp::ACPMessage::assistant_message(std::move(text_content));
                for (auto& tc : tool_calls) {
                    asst_msg.add_tool_use(tc);
                }
                history.add_message(std::move(asst_msg));
            }

            // ─── 执行工具调用 ────────────────────────────────────────
            for (auto& tc : tool_calls) {
                auto tool_result = tools_.execute(tc.name, tc.arguments);
                history.add_tool_result(tc.id, tc.name,
                    tool_result.success ? tool_result.output : tool_result.error);
                tool_call_count++;

                if (!tool_result.success) {
                    log::error_fmt("sub_agent tool failed: name={}, error={}",
                        tc.name, tool_result.error);
                }
            }
            result.tool_calls = tool_call_count;
        }

        // ─── 处理结束状态 ────────────────────────────────────────────
        if (timed_out) {
            result.status = SubAgentStatus::failed;
            result.error = "sub-agent timed out";
        } else if (final_output.empty()) {
            final_output = "(sub-agent reached max steps without final answer)";
        }

        result.full_output = final_output;
        result.output = final_output;

        if (config.auto_summary && static_cast<int>(final_output.size()) > config.max_output_chars) {
            result.output = final_output.substr(0, static_cast<size_t>(config.max_output_chars))
                          + "\n...[truncated]";
            result.was_truncated = true;
        }

        result.success = !timed_out;
        if (result.status == SubAgentStatus::pending) {
            result.status = SubAgentStatus::success;
        }

        emit_complete(task.id, result.output);

    } catch (const std::exception& e) {
        result.success = false;
        result.status = SubAgentStatus::failed;
        result.error = std::string(e.what());
        result.output = std::string("sub_agent error: ") + e.what();
        emit_error(task.id, e.what());
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

std::vector<SubAgentResult> SubAgentRuntime::execute_parallel(
    const std::vector<SubAgentTask>& tasks,
    const config::SubAgentConfig& config,
    int max_parallel) {
    start_loop();

    if (tasks.empty()) return {};
    if (max_parallel <= 0) max_parallel = 1;

    std::vector<SubAgentResult> results(tasks.size());
    std::atomic<size_t> next{0};

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= tasks.size()) break;
            results[i] = execute(tasks[i], config);
        }
    };

    int workers = std::min(max_parallel, static_cast<int>(tasks.size()));
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    return results;
}

// ─── EventBus 流式进度推送 ─────────────────────────────────────────

void SubAgentRuntime::emit_progress(const std::string& task_id, const std::string& info) {
    if (event_bus_) {
        event_bus_->publish(agent::SubAgentProgressEvent{task_id, info});
    }
}

void SubAgentRuntime::emit_complete(const std::string& task_id, const std::string& summary) {
    if (event_bus_) {
        event_bus_->publish(agent::SubAgentCompleteEvent{task_id, summary});
    }
}

void SubAgentRuntime::emit_error(const std::string& task_id, const std::string& error) {
    if (event_bus_) {
        event_bus_->publish(agent::SubAgentErrorEvent{task_id, error});
    }
}

} // namespace ben_gear::agent::runtime
