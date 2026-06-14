#include "ben_gear/agent/sub_agent.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/agent_impl.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/orchestration/event.hpp"

#include <future>
#include <algorithm>
#include <thread>
#include <optional>

namespace ben_gear::agent {

// ==================== CallbacksAdapter ====================

/// 子 Agent 回调适配器 — 将子 Agent 事件转发到父回调
class SubAgentRuntime::CallbacksAdapter : public AgentCallbacks {
public:
    CallbacksAdapter(SubAgentRuntime& runtime, const container::String& task_id)
        : runtime_(runtime), task_id_(task_id) {}

    void on_token(std::string_view) const override {
        // 子 Agent token 不转发，避免原始 markdown 流式输出导致界面混乱
        // 最终结果由主 Agent 整理后展示
    }

    void on_tool_call(const llm::ToolCallRequest& call) const override {
        log::debug_fmt("SubAgentRuntime::CallbacksAdapter: on_tool_call name={}",
                       std::string(call.name.data(), call.name.size()));
        runtime_.emit_event(SubAgentEvent::make_tool_call(task_id_, call));
    }

    void on_tool_result(const llm::ToolCallResult& result) const override {
        log::debug_fmt("SubAgentRuntime::CallbacksAdapter: on_tool_result name={}, success={}",
                       std::string(result.name.data(), result.name.size()), result.success);
        runtime_.emit_event(SubAgentEvent::make_tool_result(task_id_, result));
    }

    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name,
                           int64_t context_length) const override {
        // 累积 usage（用于汇总统计）
        std::lock_guard lock(runtime_.mutex_);
        accumulated_usage_.prompt_tokens += usage.prompt_tokens;
        accumulated_usage_.completion_tokens += usage.completion_tokens;
        accumulated_usage_.total_tokens += usage.total_tokens;
        accumulated_usage_.cached_tokens += usage.cached_tokens;
        (void)latency;
        (void)model_name;
        (void)context_length;
    }

    // thinking / tool_blocked — 不转发（子 Agent 内部细节不暴露给主 Agent）

    const llm::TokenUsage& accumulated_usage() const { return accumulated_usage_; }

private:
    SubAgentRuntime& runtime_;
    container::String task_id_;
    mutable llm::TokenUsage accumulated_usage_;
};

// ==================== SubAgentRuntime ====================

SubAgentRuntime::SubAgentRuntime(
    std::shared_ptr<SharedResources> resources,
    SubAgentConfig config,
    const AgentCallbacks* parent_callbacks,
    const container::String& parent_session_id)
    : resources_(std::move(resources))
    , config_(std::move(config))
    , parent_callbacks_(parent_callbacks)
    , parent_session_id_(parent_session_id) {
    log::info_fmt("SubAgentRuntime created: max_parallel={}, default_timeout={}ms",
                  config_.max_parallel, config_.default_timeout.count());
}

SubAgentRuntime::~SubAgentRuntime() {
    cancel_all();
}

net::Task<SubAgentResult> SubAgentRuntime::execute(
    SubAgentTask task,
    const net::CancellationToken& cancel) {
    // 生成 task_id
    if (task.id.empty()) {
        task.id = container::String(workspace::generate_uuid().c_str());
    }

    // 解析配置默认值
    const int max_steps = task.max_steps > 0 ? task.max_steps : config_.default_max_steps;
    const auto timeout = task.timeout.count() > 0 ? task.timeout : config_.default_timeout;

    log::info_fmt("SubAgentRuntime::execute: task_id={}, max_steps={}, timeout={}ms, prompt_size={}",
                  std::string(task.id.data(), task.id.size()),
                  max_steps, timeout.count(), task.prompt.size());

    // 推测执行
    if (!task.speculative_models.empty()) {
        log::info_fmt("SubAgentRuntime::execute: entering speculative mode with {} models",
                      task.speculative_models.size());
        co_return co_await execute_speculative(std::move(task), cancel);
    }

    auto effective_tool_filter = task.tool_filter;
    if (effective_tool_filter.empty() && !config_.tool_filter_default.empty()) {
        effective_tool_filter = config_.tool_filter_default;
    }

    // 创建过滤后的工具注册表
    auto filtered_registry = create_filtered_registry(
        resources_->tools(), effective_tool_filter);

    log::info_fmt("SubAgentRuntime::execute: filtered_registry_size={}", filtered_registry->size());

    // 创建子 Agent Session
    auto session = std::unique_ptr<workspace::Session>(static_cast<workspace::Session*>(create_sub_session_impl(task)));

    // 创建回调适配器
    auto adapter = std::make_unique<CallbacksAdapter>(*this, task.id);

    // 注册活跃状态，并用 watchdog 绑定父取消与 timeout。
    net::CancellationToken task_cancel;
    register_active(task.id, task_cancel);
    auto deadline = timeout.count() > 0
        ? std::optional<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + timeout)
        : std::nullopt;
    std::jthread watchdog([parent_cancel = cancel, task_cancel, deadline](std::stop_token stop) mutable {
        while (!stop.stop_requested()) {
            if (parent_cancel.is_cancelled()) {
                task_cancel.cancel();
                return;
            }
            if (deadline && std::chrono::steady_clock::now() >= *deadline) {
                task_cancel.cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // 发送 started 事件
    emit_event(SubAgentEvent::make_started(task.id, task.prompt, 1, 1));

    SubAgentResult result;
    result.task_id = task.id;

    try {
        // 在 wf_context EventLoop 上执行
        auto& wf_loop = resources_->wf_context()->loop();

        // 创建子 Agent
        Agent agent(resources_);

        log::info_fmt("SubAgentRuntime::execute: starting agent.run_session_async for task_id={}",
                      std::string(task.id.data(), task.id.size()));

        // 超时 + 取消竞争
        auto start = std::chrono::steady_clock::now();

        Agent::RunOptions run_options;
        run_options.system_prompt = task.system_prompt;
        run_options.max_tool_steps = max_steps;
        run_options.timeout = timeout;
        run_options.model_override = !task.model_override.empty()
            ? task.model_override
            : config_.model_override;

        // 执行子 Agent
        auto chat_result = co_await agent.run_session_async(
            wf_loop, *session,
            container::String(task.prompt.data(), task.prompt.size()),
            *adapter, std::move(run_options), task_cancel, filtered_registry.get());

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsed_sec = std::chrono::duration<double>(elapsed).count();

        // 收集结果（先赋值再打日志，避免日志打印默认值）
        result.success = (chat_result.status >= 200 && chat_result.status < 300);
        result.output = std::move(chat_result.text);

        log::info_fmt("SubAgentRuntime::execute: agent completed, success={}, elapsed={:.2f}s, output_size={}",
                      result.success, elapsed_sec, result.output.size());
        result.usage = adapter->accumulated_usage();
        result.latency = chat_result.latency;
        result.tool_steps = 0; // 从 history 推断
        result.status = result.success ? SubAgentStatus::completed : SubAgentStatus::failed;

        if (!result.success) {
            result.error = std::move(chat_result.error_message);
        }

        // 截断/摘要输出
        if (result.success && static_cast<int>(result.output.size()) > config_.max_output_chars) {
            result.full_output = result.output;
            result.was_truncated = true;

            log::info_fmt("SubAgentRuntime::execute: output truncated, original_size={}, max_chars={}, auto_summary={}",
                          result.full_output.size(), config_.max_output_chars, config_.auto_summary);

            if (config_.auto_summary) {
                auto summarized = co_await summarize_output(
                    wf_loop, result.output, config_.max_output_chars);
                result.output = std::move(summarized);
                result.was_summarized = true;
            } else {
                // 硬截断
                result.output.resize(config_.max_output_chars);
                result.output += "\n...[truncated]";
            }
        }

        // 发送完成事件
        if (result.success) {
            container::String summary(
                result.output.data(),
                std::min(result.output.size(), static_cast<size_t>(200)));
            emit_event(SubAgentEvent::make_completed(
                task.id, summary, result.usage, elapsed_sec,
                result.tool_steps, result.was_truncated, result.was_summarized));
        } else {
            log::error_fmt("SubAgentRuntime::execute: task failed, error={}",
                           std::string(result.error.data(), result.error.size()));
            emit_event(SubAgentEvent::make_failed(task.id, result.error));
        }

    } catch (const net::OperationCancelled&) {
        const bool timed_out = deadline && std::chrono::steady_clock::now() >= *deadline;
        result.status = timed_out ? SubAgentStatus::timeout : SubAgentStatus::cancelled;
        result.error = timed_out ? container::String("timeout") : container::String("cancelled");
        log::warn_fmt("SubAgentRuntime::execute: task {}, task_id={}",
                      timed_out ? "timeout" : "cancelled",
                      std::string(task.id.data(), task.id.size()));
        if (timed_out) {
            emit_event(SubAgentEvent::make_timeout(task.id));
        } else {
            emit_event(SubAgentEvent::make_cancelled(task.id));
        }
    } catch (const std::exception& e) {
        result.status = SubAgentStatus::failed;
        result.error = container::String(e.what());
        log::error_fmt("SubAgentRuntime::execute: task exception, task_id={}, error={}",
                      std::string(task.id.data(), task.id.size()), e.what());
        emit_event(SubAgentEvent::make_failed(task.id, result.error));
    }

    unregister_active(task.id, result.status);
    co_return result;
}

net::Task<container::Vector<SubAgentResult>> SubAgentRuntime::execute_parallel(
    container::Vector<SubAgentTask> tasks,
    const net::CancellationToken& cancel) {
    container::Vector<SubAgentResult> results;

    if (tasks.empty()) {
        co_return results;
    }

    log::info_fmt("SubAgentRuntime::execute_parallel: starting {} tasks, max_parallel={}",
                  tasks.size(), config_.max_parallel);

    // 截断超限任务
    if (static_cast<int>(tasks.size()) > config_.max_parallel) {
        log::warn_fmt("SubAgentRuntime: {} tasks exceed max_parallel={}, truncating",
                      tasks.size(), config_.max_parallel);
        tasks.resize(config_.max_parallel);
    }

    const int total = static_cast<int>(tasks.size());

    log::info_fmt("SubAgentRuntime::execute_parallel: dispatching {} tasks to thread pool", total);

    // 为每个任务创建协程并并行执行
    // 使用 promise/future 模式提交到 wf_context EventLoop
    std::vector<std::future<SubAgentResult>> futures;
    futures.reserve(tasks.size());

    for (int i = 0; i < total; ++i) {
        auto& task = tasks[i];
        if (task.id.empty()) {
            task.id = container::String(workspace::generate_uuid().c_str());
        }

        // started 事件由 execute() 统一发送，避免并行任务重复上报。

        // 创建 promise/future
        auto promise = std::make_shared<std::promise<SubAgentResult>>();
        futures.push_back(promise->get_future());

        // 提交到 wf_context EventLoop
        auto& wf_loop = resources_->wf_context()->loop();
        auto task_copy = task;
        auto cancel_copy = cancel;
        auto runtime_ptr = this;

        resources_->core_pool()->submit([&wf_loop, runtime_ptr, task_copy = std::move(task_copy),
                                          cancel_copy = std::move(cancel_copy),
                                          promise]() mutable {
            try {
                auto coro = runtime_ptr->execute(std::move(task_copy), cancel_copy);
                auto result = net::sync_wait(wf_loop, std::move(coro));
                promise->set_value(std::move(result));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }

    // 等待所有结果
    results.reserve(tasks.size());
    int success_count = 0;
    int fail_count = 0;
    for (auto& f : futures) {
        try {
            results.push_back(f.get());
            if (results.back().success) success_count++;
            else fail_count++;
        } catch (const std::exception& e) {
            SubAgentResult err_result;
            err_result.status = SubAgentStatus::failed;
            err_result.error = container::String(e.what());
            results.push_back(std::move(err_result));
            fail_count++;
        }
    }

    log::info_fmt("SubAgentRuntime::execute_parallel: all tasks done, success={}, failed={}",
                  success_count, fail_count);

    // LLM 聚合摘要
    if (config_.aggregate_parallel && results.size() > 1) {
        auto& wf_loop = resources_->wf_context()->loop();
        auto aggregate = co_await aggregate_results(wf_loop, results);
        if (!results.empty() && !aggregate.empty()) {
            results[0].artifacts["aggregate_summary"] = aggregate;
            results[0].artifacts["aggregate_result_count"] = static_cast<int64_t>(results.size());
        }
        log::info_fmt("SubAgentRuntime: parallel aggregate summary size={}", aggregate.size());
    }

    co_return results;
}

net::Task<SubAgentResult> SubAgentRuntime::execute_speculative(
    SubAgentTask task,
    const net::CancellationToken&) {
    log::info_fmt("SubAgentRuntime::execute_speculative: starting with {} models",
                  task.speculative_models.size());

    // 为每个 speculative model 创建独立子 Agent，并行启动，取最先成功
    auto& wf_loop = resources_->wf_context()->loop();

    std::vector<std::future<SubAgentResult>> futures;
    std::vector<net::CancellationToken> model_cancels;
    model_cancels.reserve(task.speculative_models.size());

    for (size_t model_index = 0; model_index < task.speculative_models.size(); ++model_index) {
        SubAgentTask spec_task;
        spec_task.id = container::String(workspace::generate_uuid().c_str());
        spec_task.prompt = task.prompt;
        spec_task.system_prompt = task.system_prompt;
        spec_task.tool_filter = task.tool_filter;
        spec_task.max_steps = task.max_steps;
        spec_task.timeout = task.timeout;
        spec_task.model_override = task.speculative_models[model_index];
        // 不递归 speculative
        // spec_task.speculative_models 为空

        net::CancellationToken model_cancel;
        model_cancels.push_back(model_cancel);

        auto promise = std::make_shared<std::promise<SubAgentResult>>();

        // 创建临时 Settings 覆盖模型
        auto spec_resources = resources_; // 共享资源

        futures.push_back(promise->get_future());

        resources_->core_pool()->submit([&wf_loop, spec_task = std::move(spec_task),
                                          model_cancel = std::move(model_cancel),
                                          promise, this]() mutable {
            try {
                auto coro = execute(std::move(spec_task), model_cancel);
                auto result = net::sync_wait(wf_loop, std::move(coro));
                promise->set_value(std::move(result));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }

    // 等待第一个成功或全部失败
    SubAgentResult first_result;
    bool found_success = false;

    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            auto result = futures[i].get();
            if (result.success && !found_success) {
                first_result = std::move(result);
                found_success = true;
                log::info_fmt("SubAgentRuntime::execute_speculative: model {} succeeded, cancelling remaining",
                              i);
                // 取消其余
                for (size_t j = i + 1; j < model_cancels.size(); ++j) {
                    model_cancels[j].cancel();
                }
                break;
            } else if (!found_success) {
                first_result = std::move(result);
                log::warn_fmt("SubAgentRuntime::execute_speculative: model {} failed, trying next", i);
            }
        } catch (const std::exception& e) {
            if (!found_success) {
                first_result.status = SubAgentStatus::failed;
                first_result.error = container::String(e.what());
                log::error_fmt("SubAgentRuntime::execute_speculative: model {} exception: {}", i, e.what());
            }
        }
    }

    if (!found_success) {
        log::error_fmt("SubAgentRuntime::execute_speculative: all {} models failed", futures.size());
    }

    co_return first_result;
}

// ---- 监控 ----

std::optional<SubAgentStatus> SubAgentRuntime::status(
    const container::String& task_id) const {
    std::lock_guard lock(mutex_);
    auto it = active_status_.find(std::string_view(task_id.data(), task_id.size()));
    if (it != active_status_.end()) {
        return it->second;
    }
    return std::nullopt;
}

container::Vector<std::pair<container::String, SubAgentStatus>>
SubAgentRuntime::all_status() const {
    std::lock_guard lock(mutex_);
    container::Vector<std::pair<container::String, SubAgentStatus>> result;
    for (const auto& [id, status] : active_status_) {
        result.push_back({container::String(id.data(), id.size()), status});
    }
    return result;
}

size_t SubAgentRuntime::active_count() const noexcept {
    std::lock_guard lock(mutex_);
    return active_tokens_.size();
}

// ---- 取消 ----

bool SubAgentRuntime::cancel(const container::String& task_id) {
    std::lock_guard lock(mutex_);
    auto it = active_tokens_.find(std::string_view(task_id.data(), task_id.size()));
    if (it != active_tokens_.end()) {
        it->second.cancel();
        return true;
    }
    return false;
}

void SubAgentRuntime::cancel_all() {
    std::lock_guard lock(mutex_);
    for (auto& [id, token] : active_tokens_) {
        token.cancel();
    }
}

// ---- 私有方法 ----

std::shared_ptr<llm::ToolRegistry> SubAgentRuntime::create_filtered_registry(
    const llm::ToolRegistry& parent,
    const container::Vector<container::String>& filter) {
    auto filtered = std::make_shared<llm::ToolRegistry>();

    // 需要排除的工具名（防止递归委派）
    static const container::String kBlockedTools[] = {
        container::String("delegate_task"),
        container::String("delegate_tasks"),
    };

    // 遍历父注册表，拷贝允许的工具
    parent.for_each([&](std::string_view name, const llm::ToolRegistryEntry& entry) {
        // 排除 delegate 工具
        for (const auto& blocked : kBlockedTools) {
            if (name == std::string_view(blocked.data(), blocked.size())) {
                return; // 跳过
            }
        }

        // 如果有 tool_filter，检查是否在列表中
        if (!filter.empty()) {
            bool found = false;
            for (const auto& allowed : filter) {
                if (name == std::string_view(allowed.data(), allowed.size())) {
                    found = true;
                    break;
                }
            }
            if (!found) return; // 不在允许列表中，跳过
        }

        // 拷贝到过滤后的注册表
        filtered->register_tool(
            container::String(name.data(), name.size()),
            entry.definition.description,
            entry.definition.parameters,
            entry.executor,
            entry.definition.read_only);
    });

    log::info_fmt("SubAgentRuntime: filtered registry size={} (parent={})",
                  filtered->size(), parent.size());
    return filtered;
}

void* SubAgentRuntime::create_sub_session_impl(
    const SubAgentTask&) {
    // 子 Agent 的 context_length
    int64_t ctx_len = config_.context_length_override > 0
        ? config_.context_length_override
        : resources_->settings().context_length;

    auto session_id = container::String(workspace::generate_uuid().c_str());

    workspace::SessionConfig config;
    config.session_id = session_id;
    config.context_length = ctx_len;
    config.context_prune = resources_->settings().context_prune;
    config.session_type = SessionType::sub_agent;
    config.parent_session_id = parent_session_id_;

    // 创建 Session（跳过情景工具注册）
    auto session = std::make_unique<workspace::Session>(
        config,
        resources_->make_session_deps(),
        resources_->tools_mut());

    // 在 DB 中创建会话元数据（含 parent_id 和 session_type）
    auto& ws_ctx = resources_->workspace_context();
    const auto& ws_name = ws_ctx.workspace_name.empty()
        ? container::String("default") : ws_ctx.workspace_name;
    resources_->history_db().create_session(
        ws_name, session_id,
        container::String("sub_agent"), // name
        SessionType::sub_agent,
        parent_session_id_);

    log::info_fmt("SubAgentRuntime: sub session created id={}, parent={}",
                  std::string(session_id.data(), session_id.size()),
                  std::string(parent_session_id_.data(), parent_session_id_.size()));

    // 返回 opaque pointer，调用方负责释放
    return session.release();
}

net::Task<container::String> SubAgentRuntime::summarize_output(
    net::EventLoop& loop,
    const container::String& output,
    int max_chars) {
    // 构建摘要提示
    std::string prompt = "请将以下内容压缩为不超过 ";
    prompt += std::to_string(max_chars);
    prompt += " 字符的摘要，保留关键发现和结论：\n\n";
    prompt += std::string(output.data(), output.size());

    llm::ChatRequest request;
    request.system_prompt = container::String("你是摘要助手，只输出摘要文本，不加额外说明。");
    request.user_prompt = container::String(prompt.data(), prompt.size());

    try {
        auto result = co_await resources_->provider().chat_async(loop, request);
        if (result.status >= 200 && result.status < 300 && !result.text.empty()) {
            co_return std::move(result.text);
        }
    } catch (const std::exception& e) {
        log::warn_fmt("SubAgentRuntime: summarize failed: {}", e.what());
    }

    // 降级：硬截断
    container::String truncated(output.data(), max_chars);
    truncated += "\n...[truncated]";
    co_return truncated;
}

net::Task<container::String> SubAgentRuntime::aggregate_results(
    net::EventLoop& loop,
    const container::Vector<SubAgentResult>& results) {
    std::string prompt = "以下是 ";
    prompt += std::to_string(results.size());
    prompt += " 个子 Agent 的并行执行结果，请综合摘要：\n\n";

    for (size_t i = 0; i < results.size(); ++i) {
        prompt += "[子 Agent ";
        prompt += std::to_string(i + 1);
        prompt += "] ";
        if (results[i].success) {
            prompt += std::string(results[i].output.data(), results[i].output.size());
        } else {
            prompt += "失败: ";
            prompt += std::string(results[i].error.data(), results[i].error.size());
        }
        prompt += "\n\n";
    }

    llm::ChatRequest request;
    request.system_prompt = container::String("你是摘要助手，综合多个子任务结果为简洁摘要。");
    request.user_prompt = container::String(prompt.data(), prompt.size());

    try {
        auto result = co_await resources_->provider().chat_async(loop, request);
        if (result.status >= 200 && result.status < 300 && !result.text.empty()) {
            co_return std::move(result.text);
        }
    } catch (const std::exception& e) {
        log::warn_fmt("SubAgentRuntime: aggregate failed: {}", e.what());
    }

    // 降级：简单拼接
    container::String fallback;
    for (size_t i = 0; i < results.size(); ++i) {
        if (!fallback.empty()) fallback += "\n";
        fallback += results[i].output;
    }
    co_return fallback;
}

namespace {

orchestration::ExecutionEvent to_execution_event(const SubAgentEvent& event) {
    orchestration::ExecutionEvent out;
    out.execution_id = event.task_id;
    out.kind = orchestration::ExecutionKind::sub_agent;
    out.type = [&] {
        switch (event.type) {
        case SubAgentEventType::started: return orchestration::ExecutionEventType::started;
        case SubAgentEventType::tool_call: return orchestration::ExecutionEventType::tool_call;
        case SubAgentEventType::tool_result: return orchestration::ExecutionEventType::tool_result;
        case SubAgentEventType::token_output: return orchestration::ExecutionEventType::token;
        case SubAgentEventType::completed: return orchestration::ExecutionEventType::completed;
        case SubAgentEventType::failed: return orchestration::ExecutionEventType::failed;
        case SubAgentEventType::cancelled: return orchestration::ExecutionEventType::cancelled;
        case SubAgentEventType::timeout: return orchestration::ExecutionEventType::timeout;
        }
        return orchestration::ExecutionEventType::progress;
    }();
    out.status = [&] {
        switch (event.type) {
        case SubAgentEventType::started:
        case SubAgentEventType::tool_call:
        case SubAgentEventType::tool_result:
        case SubAgentEventType::token_output:
            return orchestration::ExecutionStatus::running;
        case SubAgentEventType::completed:
            return orchestration::ExecutionStatus::succeeded;
        case SubAgentEventType::failed:
            return orchestration::ExecutionStatus::failed;
        case SubAgentEventType::cancelled:
            return orchestration::ExecutionStatus::cancelled;
        case SubAgentEventType::timeout:
            return orchestration::ExecutionStatus::timeout;
        }
        return orchestration::ExecutionStatus::running;
    }();

    if (const auto* started = std::get_if<SubAgentStartedData>(&event.payload)) {
        out.message = started->prompt_summary;
        out.payload.fields[container::String("index")] = container::String(std::to_string(started->index));
        out.payload.fields[container::String("total")] = container::String(std::to_string(started->total));
    } else if (const auto* token = std::get_if<SubAgentTokenData>(&event.payload)) {
        out.payload.text = token->token;
    } else if (const auto* failed = std::get_if<SubAgentFailedData>(&event.payload)) {
        out.message = failed->error;
    } else if (const auto* completed = std::get_if<SubAgentCompletedData>(&event.payload)) {
        out.payload.text = completed->output_summary;
        out.usage = completed->usage;
        out.latency.total_seconds = completed->elapsed_seconds;
        out.payload.fields[container::String("tool_steps")] = container::String(std::to_string(completed->tool_steps));
        out.payload.fields[container::String("was_truncated")] = container::String(completed->was_truncated ? "true" : "false");
        out.payload.fields[container::String("was_summarized")] = container::String(completed->was_summarized ? "true" : "false");
    } else if (const auto* call = std::get_if<llm::ToolCallRequest>(&event.payload)) {
        out.payload.fields[container::String("tool_name")] = call->name;
        out.payload.text = call->arguments.dump();
    } else if (const auto* result = std::get_if<llm::ToolCallResult>(&event.payload)) {
        out.payload.fields[container::String("tool_name")] = result->name;
        out.payload.fields[container::String("success")] = container::String(result->success ? "true" : "false");
        out.payload.text = result->output;
    }

    return out;
}

} // namespace

void SubAgentRuntime::emit_event(const SubAgentEvent& event) const {
    if (parent_callbacks_) {
        parent_callbacks_->on_execution_event(to_execution_event(event));
        parent_callbacks_->on_sub_agent_event(event);
    }
}

void SubAgentRuntime::register_active(
    const container::String& task_id,
    const net::CancellationToken& token) {
    std::lock_guard lock(mutex_);
    auto key = std::string(task_id.data(), task_id.size());
    active_tokens_[key] = token;
    active_status_[key] = SubAgentStatus::running;
}

void SubAgentRuntime::unregister_active(
    const container::String& task_id,
    SubAgentStatus final_status) {
    std::lock_guard lock(mutex_);
    auto key = std::string(task_id.data(), task_id.size());
    auto status_key = container::String(key.data(), key.size());
    active_tokens_.erase(status_key);
    active_status_[status_key] = final_status;
    completed_status_order_.push_back(status_key);

    constexpr size_t kMaxCompletedStatus = 128;
    while (completed_status_order_.size() > kMaxCompletedStatus) {
        const auto& oldest = completed_status_order_.front();
        if (active_tokens_.find(oldest) == active_tokens_.end()) {
            active_status_.erase(oldest);
        }
        completed_status_order_.erase(completed_status_order_.begin());
    }
}

} // namespace ben_gear::agent
