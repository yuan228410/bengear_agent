#pragma once

#include <string>
#include <vector>

#include <chrono>
#include <cstdint>

namespace ben_gear::config {

enum class SessionType : uint8_t {
    main,
    sub_agent
};

struct SubAgentConfig {
    int max_parallel = 5;
    int default_max_steps = 20;
    std::chrono::milliseconds default_timeout{120000};
    bool auto_summary = false;
    int max_output_chars = 0;
    std::vector<std::string> tool_filter_default;

    /// 最大递归深度（子 Agent 委派子 Agent），防止无限递归
    /// 0=子 Agent 不允许再委托子 Agent（默认）
    int max_agent_depth = 0;

    /// 子 Agent 不可见的工具黑名单
    /// 排除原则：上下文应由主 Agent 在 task prompt 中提供，子 Agent 不应自主读取；
    /// 写操作影响主 Agent 状态；递归 delegation/工作流/TODO 等复杂操作子 Agent 不应执行。
    std::vector<std::string> exclude_tools = {
        // 递归 delegation
        "delegate_task",
        "delegate_tasks",
        // 记忆/灵魂/规则/用户（读写全禁，上下文由主 Agent 在 prompt 中提供）
        "read_memory",
        "write_memory",
        "recall",
        "read_soul",
        "write_soul",
        "read_rules",
        "write_rules",
        "read_user",
        "write_user",
        "append_episode",
        "read_episode",
        "read_episode_range",
        // 子 Agent 管理
        "subagent_list",
        "subagent_create",
        "subagent_remove",
        // 团队管理
        "create_team",
        "team_create",
        "team_add_member",
        "team_remove_member",
        "team_update_member",
        "team_update",
        "run_team",
        "team_assign",
        "team_broadcast",
        "team_send",
        "team_read_messages",
        "team_list",
        "team_status",
        // TODO 管理
        "update_todo",
        // 风险操作
        "delete_file",
        "env_set",
        // 会话/工作空间管理
        "list_workspaces",
        "create_workspace",
        "remove_workspace",
        "restore_workspace",
        // 历史管理
        "delete_history",
        // 技能管理
        "get_skill",
        "install_skill",
        "remove_skill",
        "enable_skill",
        "disable_skill",
        "list_skills"
    };

    std::string model_override;

    /// 自定义子 Agent 目录（.md 文件，含 frontmatter），默认 ~/.bengear/sub_agents/
    std::string sub_agents_dir;
    int64_t context_length_override = 0;
    bool aggregate_parallel = true;
};

} // namespace ben_gear::config
