#pragma once

#include "capabilities/tool/types.hpp"
#include "llm/usage.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"

#include <string_view>

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

// ─── 聚合结构体（需要全部三个接口的地方用这个）────────────────────

struct AgentEventSinks {
    StreamEventSink& stream;
    ToolEventSink& tool;
    OrchestrationEventSink& orch;
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

} // namespace ben_gear::agent
