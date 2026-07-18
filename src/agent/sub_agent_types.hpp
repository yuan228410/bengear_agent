#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace ben_gear::agent {

/// SubAgent 任务描述
struct SubAgentTask {
    std::string id;
    std::string prompt;
    std::string system_prompt;
    std::vector<std::string> tool_filter;
    int max_steps = 0;
    std::chrono::milliseconds timeout{0};
    std::vector<std::string> speculative_models;
};

/// SubAgent 执行状态
enum class SubAgentStatus { pending, running, success, failed, cancelled };

/// SubAgent 执行结果（完整版）
struct SubAgentResult {
    std::string task_id;
    bool success = false;
    SubAgentStatus status = SubAgentStatus::pending;
    std::string output;
    std::string full_output;
    std::string error;
    int tool_calls = 0;
    std::chrono::milliseconds duration{0};
    bool was_truncated = false;
    bool was_summarized = false;
};

} // namespace ben_gear::agent
