#include "agent/runtime/sub_agent_runtime.hpp"
#include <mutex>
#include <thread>
#include <chrono>

#include "llm/conversation_history.hpp"
#include "acp/core/message.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ben_gear::agent::runtime {

using Json = ben_gear::Json;

// 全局递归深度计数器：防止子 Agent 无限嵌套调用。
// 使用 std::atomic<int> 而非 thread_local，因为 execute_parallel
// 会在多个 worker 线程中并发调用 execute()，必须跨线程共享深度计数。
// execute 入口检查深度，超过 max_agent_depth 则拒绝执行。
static std::atomic<int> g_agent_depth{0};

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
    // 递归深度检查：防止子 Agent 嵌套调用超过限制
    const int max_depth = config.max_agent_depth;
    int current_depth = g_agent_depth.load();
    if (current_depth > max_depth) {
        SubAgentResult result;
        result.task_id = task.id;
        result.success = false;
        result.status = SubAgentStatus::failed;
        result.error = "max agent depth exceeded (" + std::to_string(current_depth) +
                       " > " + std::to_string(max_depth) + ")";
        result.output = result.error;
        log::warn_fmt("sub_agent: depth limit hit, task={} depth={}", task.id, current_depth);
        return result;
    }

    start_loop();

    // RAII 深度计数器：execute 期间递增，退出时递减
    ++g_agent_depth;
    struct DepthGuard {
        ~DepthGuard() { --g_agent_depth; }
    } depth_guard;

    SubAgentResult result;
    result.task_id = task.id;
    result.status = SubAgentStatus::running;
    auto start = std::chrono::steady_clock::now();

    // 发送 SubAgentStartEvent，让前端可以感知子 Agent 启动
    if (event_bus_) {
        event_bus_->publish(agent::SubAgentStartEvent{task.id, task.prompt});
    }
    emit_progress(task.id, "started");

    try {
        // 使用 task 携带的 system prompt（team Agent 的人格描述）；
        // 普通 sub-agent 不设置则为空，保持原有行为
        llm::ConversationHistory history;
        history.set_system_prompt(task.system_prompt);

        log::info_fmt("sub_agent: task={} system_prompt={}bytes prompt={}bytes tools={}",
            task.id, task.system_prompt.size(), task.prompt.size(), sub_agent_tools_->size());
        history.add_user(task.prompt);

        // ─── 动态工具过滤（两层）──────────────────────────────────
        // 第一层：exclude_tools 黑名单。sub_agent_tools_ 是基于
        // default_config_.exclude_tools 在构造时过滤的；当调用方
        //（如 PersistentAgent）传入不同的 exclude_tools 时，需要
        // 基于原始 tools_ 重新动态过滤，确保放行的工具正确可见。
        std::unique_ptr<capabilities::tool::ToolRegistry> dynamic_tools;
        const capabilities::tool::ToolRegistry* base_active = sub_agent_tools_.get();
        if (config.exclude_tools != default_config_.exclude_tools) {
            dynamic_tools = tools_.without(config.exclude_tools);
            base_active = dynamic_tools.get();
            log::debug_fmt("sub_agent: dynamic exclude filter, config={} default={}",
                config.exclude_tools.size(), default_config_.exclude_tools.size());
        }

        // 第二层：task.tool_filter 白名单。自定义子 Agent 通过
        // frontmatter 中的 tools 字段指定，白名单之外的工具被排除。
        const capabilities::tool::ToolRegistry* active_tools = base_active;
        std::unique_ptr<capabilities::tool::ToolRegistry> filtered_tools;
        if (!task.tool_filter.empty()) {
            std::vector<std::string> excluded;
            for (const auto& name : base_active->tool_names()) {
                if (std::find(task.tool_filter.begin(), task.tool_filter.end(), name)
                    == task.tool_filter.end()) {
                    excluded.push_back(name);
                }
            }
            if (!excluded.empty()) {
                filtered_tools = base_active->without(excluded);
                active_tools = filtered_tools.get();
                log::debug_fmt("sub_agent: tool_filter whitelist applied, tools={}/{}",
                    filtered_tools->size(), sub_agent_tools_->size());
            }
        }

        int max_steps = config.default_max_steps > 0 ? config.default_max_steps : 10;
        int tool_call_count = 0;
        std::string final_output;

        // 终止原因枚举，区分超时 / 熔断 / 正常结束，替代原来语义模糊的 timed_out
        enum class AbortReason { kNone, kTimeout, kCircuitBreaker };
        AbortReason abort_reason = AbortReason::kNone;
        std::string abort_detail;  // 熔断时记录失败的工具名，用于错误信息

        // 工具失败熔断：同一工具连续失败超过阈值则中断，避免空转到 max_steps
        std::unordered_map<std::string, int> tool_fail_counts;
        const int k_max_tool_fails = 3;

        // ─── ReAct 循环 ──────────────────────────────────────────────
        for (int step = 0; step < max_steps; ++step) {
            emit_progress(task.id, "step " + std::to_string(step + 1) + "/" + std::to_string(max_steps));

            // 超时检查
            if (task.timeout.count() > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                if (elapsed > task.timeout) {
                    final_output = "(sub-agent timed out)";
                    abort_reason = AbortReason::kTimeout;
                    break;
                }
            }

            // 调用 LLM
            auto response = net::sync_wait(sub_loop_,
                provider_.chat(sub_loop_, history, *active_tools, {}, {},
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
            // 使用 active_tools（已过滤）而非 tools_（原始），确保
            // exclude_tools 和 tool_filter 不仅影响 LLM schema，也约束实际执行
            for (auto& tc : tool_calls) {
                auto tool_result = active_tools->execute(tc.name, tc.arguments);
                history.add_tool_result(tc.id, tc.name,
                    tool_result.success ? tool_result.output : tool_result.error);
                tool_call_count++;

                if (!tool_result.success) {
                    log::error_fmt("sub_agent tool failed: name={}, error={}",
                        tc.name, tool_result.error);
                    // 熔断：同一工具连续失败超阈值则终止，避免空转浪费 token
                    if (++tool_fail_counts[tc.name] >= k_max_tool_fails) {
                        final_output = "(sub-agent aborted: tool '" + tc.name +
                                       "' failed " + std::to_string(k_max_tool_fails) + " times)";
                        abort_reason = AbortReason::kCircuitBreaker;
                        abort_detail = tc.name;
                        break;
                    }
                } else {
                    tool_fail_counts[tc.name] = 0;  // 成功则重置计数
                }
            }
        }
        // tool_calls 在循环结束后统一赋值（原来在内层循环中每次工具调用都赋值，多余操作）
        result.tool_calls = tool_call_count;

        // ─── 处理结束状态（区分超时 vs 熔断 vs 正常）──────────────
        switch (abort_reason) {
        case AbortReason::kTimeout:
            result.status = SubAgentStatus::failed;
            result.error = "sub-agent timed out";
            break;
        case AbortReason::kCircuitBreaker:
            result.status = SubAgentStatus::failed;
            result.error = "sub-agent circuit breaker: tool '" + abort_detail +
                           "' failed " + std::to_string(k_max_tool_fails) +
                           " consecutive times";
            break;
        case AbortReason::kNone:
            if (final_output.empty()) {
                final_output = "(sub-agent reached max steps without final answer)";
            }
            break;
        }

        result.full_output = final_output;
        result.output = final_output;

        if (config.auto_summary && static_cast<int>(final_output.size()) > config.max_output_chars) {
            result.output = final_output.substr(0, static_cast<size_t>(config.max_output_chars))
                          + "\n...[truncated]";
            result.was_truncated = true;
        }

        result.success = (abort_reason == AbortReason::kNone);
        // running 状态表示正常走完了 ReAct 循环（可能成功或 max steps）
        if (result.status == SubAgentStatus::running) {
            result.status = result.success ? SubAgentStatus::success : SubAgentStatus::failed;
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
