#pragma once

#include <cstdint>
#include <string_view>

// 前向声明 — agent_core 不依赖 tool/llm/orchestration 的完整头文件
namespace ben_gear::capabilities::tool {
struct ToolCallRequest;
struct ToolCallResult;
} // namespace ben_gear::capabilities::tool

namespace ben_gear::llm {
struct TokenUsage;
struct RequestLatency;
} // namespace ben_gear::llm

namespace ben_gear::orchestration {
struct ExecutionEvent;
struct TodoItem;
} // namespace ben_gear::orchestration

namespace ben_gear::agent {

// ─── 三层事件接口（接口隔离）──────────────────────────────────────

/// LLM 流式输出事件
class StreamEventSink {
public:
    virtual ~StreamEventSink() = default;
    virtual void on_token(std::string_view token) const = 0;
    virtual void on_thinking(std::string_view token) const = 0;
    virtual void on_response_stats(const llm::TokenUsage& usage,
                                   const llm::RequestLatency& latency,
                                   std::string_view model_name = {},
                                   int64_t context_length = 0) const = 0;
};

/// 工具调用事件
class ToolEventSink {
public:
    virtual ~ToolEventSink() = default;
    virtual void on_tool_call(const capabilities::tool::ToolCallRequest& call) const = 0;
    virtual void on_tool_result(const capabilities::tool::ToolCallResult& result) const = 0;
    virtual void on_tool_blocked(std::string_view tool_name,
                                 std::string_view reason) const = 0;
};

/// 编排/计划事件
class OrchestrationEventSink {
public:
    virtual ~OrchestrationEventSink() = default;
    virtual void on_execution_event(const orchestration::ExecutionEvent& event) const = 0;
    virtual void on_todo_update(const orchestration::TodoItem& item,
                                std::string_view action) const = 0;
};

/// SubAgent 事件
class SubAgentEventSink {
public:
    virtual ~SubAgentEventSink() = default;
    virtual void on_sub_agent_start(const std::string& task_id,
                                    const std::string& prompt) const = 0;
    virtual void on_sub_agent_progress(const std::string& task_id,
                                       const std::string& info) const = 0;
    virtual void on_sub_agent_complete(const std::string& task_id,
                                       const std::string& summary) const = 0;
    virtual void on_sub_agent_error(const std::string& task_id,
                                    const std::string& error) const = 0;
};

// ─── 聚合结构体（需要全部四个接口的地方用这个）────────────────────

struct AgentEventSinks {
    StreamEventSink& stream;
    ToolEventSink& tool;
    OrchestrationEventSink& orch;
    SubAgentEventSink& sub_agent;
};

// ─── Null 实现（所有三个接口）─────────────────────────────────────

class NullStreamSink : public StreamEventSink {
public:
    void on_token(std::string_view) const override {}
    void on_thinking(std::string_view) const override {}
    void on_response_stats(const llm::TokenUsage&, const llm::RequestLatency&,
                           std::string_view, int64_t) const override {}
};

class NullToolSink : public ToolEventSink {
public:
    void on_tool_call(const capabilities::tool::ToolCallRequest&) const override {}
    void on_tool_result(const capabilities::tool::ToolCallResult&) const override {}
    void on_tool_blocked(std::string_view, std::string_view) const override {}
};

class NullOrchestrationSink : public OrchestrationEventSink {
public:
    void on_execution_event(const orchestration::ExecutionEvent&) const override {}
    void on_todo_update(const orchestration::TodoItem&, std::string_view) const override {}
};

class NullSubAgentEventSink : public SubAgentEventSink {
public:
    void on_sub_agent_start(const std::string&, const std::string&) const override {}
    void on_sub_agent_progress(const std::string&, const std::string&) const override {}
    void on_sub_agent_complete(const std::string&, const std::string&) const override {}
    void on_sub_agent_error(const std::string&, const std::string&) const override {}
};

} // namespace ben_gear::agent
