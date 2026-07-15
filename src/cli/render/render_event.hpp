#pragma once


namespace ben_gear::cli {

enum class RenderExecutionKind {
    unknown,
    sub_agent,
};

enum class RenderExecutionEventType {
    unknown,
    started,
    tool_call,
    tool_result,
    token,
    completed,
    failed,
    cancelled,
    timeout,
};

/// UI-facing execution event model.
///
/// This is intentionally detached from orchestration payload internals: runtime
/// events are translated at the adapter boundary, while renderers consume only
/// flattened display data.
struct RenderExecutionEvent {
    RenderExecutionKind kind = RenderExecutionKind::unknown;
    RenderExecutionEventType type = RenderExecutionEventType::unknown;

    std::string message;
    std::string text;
    std::string tool_name;
    std::string index;
    std::string total;
    std::string tool_steps;

    bool was_summarized = false;
    bool was_truncated = false;

    double total_seconds = 0.0;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

}  // namespace ben_gear::cli
