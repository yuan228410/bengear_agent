#pragma once

#include "ben_gear/agent/sub_agent_config.hpp"
#include "ben_gear/llm/usage.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <variant>

namespace ben_gear::agent {

namespace container = base::container;

// ==================== 子 Agent 状态 ====================

enum class SubAgentStatus : uint8_t {
    pending, running, completed, failed, cancelled, timeout
};

// ==================== 子 Agent 事件 ====================

enum class SubAgentEventType : uint8_t {
    started, tool_call, tool_result, token_output,
    completed, failed, cancelled, timeout
};

inline container::String to_string(SubAgentEventType t) {
    switch (t) {
    case SubAgentEventType::started: return container::String("started");
    case SubAgentEventType::tool_call: return container::String("tool_call");
    case SubAgentEventType::tool_result: return container::String("tool_result");
    case SubAgentEventType::token_output: return container::String("token_output");
    case SubAgentEventType::completed: return container::String("completed");
    case SubAgentEventType::failed: return container::String("failed");
    case SubAgentEventType::cancelled: return container::String("cancelled");
    case SubAgentEventType::timeout: return container::String("timeout");
    default: return container::String("unknown");
    }
}

struct SubAgentStartedData {
    container::String prompt_summary;
    int index = 0;
    int total = 0;
};

struct SubAgentTokenData {
    container::String token;
};

struct SubAgentFailedData {
    container::String error;
};

struct SubAgentCompletedData {
    container::String output_summary;  // 截断至 200 字符的输出摘要
    llm::TokenUsage usage;             // 子 Agent 累计 token 用量
    double elapsed_seconds = 0.0;      // 执行耗时
    int tool_steps = 0;                // 工具调用步数
    bool was_truncated = false;        // 输出是否被截断
    bool was_summarized = false;       // 输出是否经 LLM 摘要
};

struct SubAgentEvent {
    container::String task_id;
    SubAgentEventType type;
    std::chrono::steady_clock::time_point timestamp;

    std::variant<
        std::monostate,
        SubAgentStartedData,
        SubAgentTokenData,
        SubAgentFailedData,
        llm::ToolCallRequest,
        llm::ToolCallResult,
        SubAgentCompletedData
    > payload;

    static SubAgentEvent make_started(const container::String& task_id,
                                      const container::String& prompt_summary,
                                      int index, int total) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::started;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = SubAgentStartedData{prompt_summary, index, total};
        return e;
    }

    static SubAgentEvent make_tool_call(const container::String& task_id,
                                        const llm::ToolCallRequest& call) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::tool_call;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = call;
        return e;
    }

    static SubAgentEvent make_tool_result(const container::String& task_id,
                                          const llm::ToolCallResult& result) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::tool_result;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = result;
        return e;
    }

    static SubAgentEvent make_token(const container::String& task_id,
                                    const container::String& token) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::token_output;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = SubAgentTokenData{token};
        return e;
    }

    static SubAgentEvent make_completed(const container::String& task_id,
                                        const container::String& output_summary,
                                        const llm::TokenUsage& usage = {},
                                        double elapsed_seconds = 0.0,
                                        int tool_steps = 0,
                                        bool was_truncated = false,
                                        bool was_summarized = false) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::completed;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = SubAgentCompletedData{
            output_summary, usage, elapsed_seconds, tool_steps,
            was_truncated, was_summarized};
        return e;
    }

    static SubAgentEvent make_failed(const container::String& task_id,
                                     const container::String& error) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::failed;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = SubAgentFailedData{error};
        return e;
    }

    static SubAgentEvent make_cancelled(const container::String& task_id) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::cancelled;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = std::monostate{};
        return e;
    }

    static SubAgentEvent make_timeout(const container::String& task_id) {
        SubAgentEvent e;
        e.task_id = task_id;
        e.type = SubAgentEventType::timeout;
        e.timestamp = std::chrono::steady_clock::now();
        e.payload = std::monostate{};
        return e;
    }
};

// ==================== 子 Agent 结果 ====================

struct SubAgentResult {
    container::String task_id;
    bool success = false;
    SubAgentStatus status = SubAgentStatus::pending;
    container::String output;
    container::String full_output;
    container::String error;
    llm::TokenUsage usage;
    llm::RequestLatency latency;
    int tool_steps = 0;
    Json artifacts;
    bool was_truncated = false;
    bool was_summarized = false;
};

// ==================== 子 Agent 任务描述 ====================

struct SubAgentTask {
    container::String id;
    container::String prompt;
    container::String system_prompt;
    container::Vector<container::String> tool_filter;
    int max_steps = 0;
    std::chrono::milliseconds timeout{0};
    container::Vector<container::String> speculative_models;
};

// ==================== 前向声明 ====================

class SharedResources;
class AgentCallbacks;

// ==================== 子 Agent 运行时 ====================

class SubAgentRuntime {
public:
    explicit SubAgentRuntime(
        std::shared_ptr<SharedResources> resources,
        SubAgentConfig config,
        const AgentCallbacks* parent_callbacks = nullptr,
        const container::String& parent_session_id = {});

    ~SubAgentRuntime();

    ::ben_gear::net::Task<SubAgentResult> execute(
        SubAgentTask task,
        const ::ben_gear::net::CancellationToken& cancel = {});

    ::ben_gear::net::Task<container::Vector<SubAgentResult>> execute_parallel(
        container::Vector<SubAgentTask> tasks,
        const ::ben_gear::net::CancellationToken& cancel = {});

    // ---- 监控（线程安全）----
    std::optional<SubAgentStatus> status(const container::String& task_id) const;
    container::Vector<std::pair<container::String, SubAgentStatus>> all_status() const;
    size_t active_count() const noexcept;

    // ---- 取消 ----
    bool cancel(const container::String& task_id);
    void cancel_all();

    /// è®¾ç½®ç¶åè°ï¼æ¯æ¬¡è¯·æ±åç±ä¸å±è®¾ç½®ï¼ç¡®ä¿äºä»¶å®æ¶è½¬åå° UIï¼
    void set_parent_callbacks(const AgentCallbacks* callbacks) {
        parent_callbacks_ = callbacks;
    }

    // ---- 资源访问（实现文件中定义，避免头文件循环依赖）----
    std::shared_ptr<SharedResources> resources() const noexcept { return resources_; }

private:
    static std::shared_ptr<llm::ToolRegistry> create_filtered_registry(
        const llm::ToolRegistry& parent,
        const container::Vector<container::String>& filter);

    void* create_sub_session_impl(const SubAgentTask& task);
    net::Task<container::String> summarize_output(net::EventLoop& loop,
        const container::String& output, int max_chars);
    net::Task<container::String> aggregate_results(net::EventLoop& loop,
        const container::Vector<SubAgentResult>& results);
    net::Task<SubAgentResult> execute_speculative(SubAgentTask task,
        const net::CancellationToken& cancel);

    class CallbacksAdapter;
    void emit_event(const SubAgentEvent& event) const;
    void register_active(const container::String& task_id,
                         const ::ben_gear::net::CancellationToken& token);
    void unregister_active(const container::String& task_id,
                           SubAgentStatus final_status);

    std::shared_ptr<SharedResources> resources_;
    SubAgentConfig config_;
    const AgentCallbacks* parent_callbacks_;
    container::String parent_session_id_;
    container::Map<container::String, ::ben_gear::net::CancellationToken> active_tokens_;
    container::Map<container::String, SubAgentStatus> active_status_;
    mutable std::mutex mutex_;
};

} // namespace ben_gear::agent

namespace ben_gear {
using SubAgentRuntime = agent::SubAgentRuntime;
using SubAgentEvent = agent::SubAgentEvent;
using SubAgentResult = agent::SubAgentResult;
using SubAgentTask = agent::SubAgentTask;
using SubAgentConfig = agent::SubAgentConfig;
using SubAgentStatus = agent::SubAgentStatus;
using SubAgentEventType = agent::SubAgentEventType;
using SessionType = agent::SessionType;
}
