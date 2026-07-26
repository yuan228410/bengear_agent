#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Agent 层事件类型 — 纯数据 struct，无虚函数，按类型索引分发
//
//  设计原则：
//  - agent 层是基础层，事件类型不得引用上层（llm / acp / orchestration）的具体类型
//  - 所有字符串字段使用 std::string（拥有所有权），避免悬空引用
//  - 工具调用用 opaque name/args_json 替代具体 ToolCallRequest 引用
//  - 用量统计用原始字段替代 llm::TokenUsage 引用
//
//  重要约束：EventBus::publish() 在锁外同步调用所有 handler。
//  事件对象在 publish() 返回后即销毁，因此所有字段必须拥有所有权。
//  禁止使用 std::string_view 等非拥有型类型。
// ═══════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>

namespace ben_gear::agent {

// ─── LLM 事件 ────────────────────────────────────────────────────

/// LLM 流式输出事件
struct TokenEvent {
    std::string token;           // 拥有所有权，避免 publish 后悬空
    int cumulative_tokens = 0;   // 本次响应已输出的 token 累计数
    bool is_end = false;
};

/// LLM thinking 块输出事件（可选显示）
struct ThinkingEvent {
    std::string token;  // 拥有所有权
};

/// LLM 响应完成统计事件
struct ResponseStatsEvent {
    int64_t prompt_tokens = 0;
    int64_t completion_tokens = 0;
    int64_t total_tokens = 0;

    double total_seconds = 0.0;
    double ttfb_seconds = 0.0;
    bool has_ttfb = false;

    std::string model_name = std::string();
    int64_t context_length = 0;
    int turn_index = 0;
    int session_prompt_tokens = 0;
    int session_completion_tokens = 0;
};

// ─── 工具调用事件 ────────────────────────────────────────────────

/// 工具调用事件（opaque name + args_json，替代 ToolCallRequest 引用）
struct ToolCallEvent {
    std::string name;            // 拥有所有权
    std::string args_json;       // 序列化后的 JSON 参数
    std::string tool_call_id;    // 拥有所有权
};

/// 工具调用结果事件（opaque output，替代 ToolCallResult 引用）
struct ToolResultEvent {
    std::string name;            // 拥有所有权
    std::string output;          // 工具返回的原始输出
    std::string tool_call_id;    // 拥有所有权
    bool ok = true;
    std::string error_message;   // 仅在 !ok 时有效
};

/// 工具被拦截事件（安全/计划模式拦截）
struct ToolBlockedEvent {
    std::string tool_name;       // 拥有所有权
    std::string reason;          // 拥有所有权
};

// ─── 编排/计划事件 ───────────────────────────────────────────────

/// 执行事件（opaque json_payload，替代 ExecutionEvent 引用）
struct ExecutionPlanEvent {
    std::string json_payload;    // 序列化后的 ExecutionEvent JSON
    std::string execution_id;    // 拥有所有权
    std::string kind;            // "tool" / "llm" / "sub_agent"
};

/// TODO 更新事件
struct TodoUpdateEvent {
    std::string todo_id;
    std::string session_id;
    std::string workspace;
    std::string title;
    std::string status;    // "pending" / "running" / "succeeded" / "failed" / "blocked"
    std::string action;    // "created" / "updated" / "deleted" / "clear"
    int progress = 0;
    std::string summary;   // 执行结果摘要
};

// ─── SubAgent 事件 ───────────────────────────────────────────────

struct SubAgentStartEvent {
    std::string task_id;
    std::string prompt;
};

struct SubAgentProgressEvent {
    std::string task_id;
    std::string info;
};

struct SubAgentCompleteEvent {
    std::string task_id;
    std::string summary;
};

struct SubAgentErrorEvent {
    std::string task_id;
    std::string error;
};

// ─── Team 事件 ─────────────────────────────────────────────────

struct TeamStartEvent {
    std::string team_id;
    std::string execution_id;
    std::string objective;
};

struct TeamStageEvent {
    std::string team_id;
    std::string execution_id;
    std::string stage_id;
    bool completed = false;
    std::string summary;
};

struct TeamMemberEvent {
    std::string team_id;
    std::string execution_id;
    std::string agent_id;
    std::string agent_name;
    std::string state;  // "idle" / "busy" / "sleeping"
    bool has_error = false;
    std::string error;
};

struct TeamArtifactEvent {
    std::string team_id;
    std::string execution_id;
    std::string key;
    std::string preview;
};

struct TeamMessageEvent {
    std::string team_id;
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
};

} // namespace ben_gear::agent