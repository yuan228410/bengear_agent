#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// 前向声明 — 事件定义不依赖具体类型完整头文件
namespace ben_gear::acp { struct ToolCallRequest; struct ToolCallResult; }
namespace ben_gear::llm { struct TokenUsage; struct RequestLatency; }
namespace ben_gear::orchestration { struct ExecutionEvent; struct TodoItem; }

namespace ben_gear::agent {

// ═══════════════════════════════════════════════════════════════════
//  事件类型 — 普通 struct，无虚函数，按类型索引分发
// ═══════════════════════════════════════════════════════════════════

/// LLM 流式输出事件
struct TokenEvent {
    std::string_view token;
    int cumulative_tokens = 0;       // 本次响应已输出的 token 累计数（客户端计数）
    bool is_end = false;              // 流结束标记
    const llm::TokenUsage* usage = nullptr;  // LLM 返回的实时 usage（可能为 nullptr）
};

struct ThinkingEvent {
    std::string_view token;
};

struct ResponseStatsEvent {
    const llm::TokenUsage& usage;
    const llm::RequestLatency& latency;
    std::string_view model_name;
    int64_t context_length = 0;
    int turn_index = 0;             // 本次会话的第几轮对话
    int session_prompt_tokens = 0;  // 会话累计 input tokens
    int session_completion_tokens = 0; // 会话累计 output tokens
};

/// 工具调用事件
struct ToolCallEvent {
    const acp::ToolCallRequest& call;
};

struct ToolResultEvent {
    const acp::ToolCallResult& result;
};

struct ToolBlockedEvent {
    std::string_view tool_name;
    std::string_view reason;
};

/// 编排/计划事件
struct ExecutionPlanEvent {
    const orchestration::ExecutionEvent& event;
};

struct TodoUpdateEvent {
    const orchestration::TodoItem& item;
    std::string_view action;
};

/// SubAgent 事件
struct SubAgentStartEvent {
    const std::string& task_id;
    const std::string& prompt;
};

struct SubAgentProgressEvent {
    const std::string& task_id;
    const std::string& info;
};

struct SubAgentCompleteEvent {
    const std::string& task_id;
    const std::string& summary;
};

struct SubAgentErrorEvent {
    const std::string& task_id;
    const std::string& error;
};

} // namespace ben_gear::agent
