#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ben_gear::team {

/// Agent 角色
enum class TeamRole : uint8_t {
    lead,       // 主导：分配任务、评审结果、介入冲突
    member,     // 执行：完成分配的工作
};

/// 协作策略
enum class TeamStrategy : uint8_t {
    sequential,     // 串行：按 stage 定义的顺序执行
    parallel,       // 并行：所有成员同时工作
    pipeline,       // 流水线：按 stage 顺序，每个 stage 一个 Agent
};

/// Agent 生命周期状态
enum class AgentLifecycle : uint8_t {
    idle,       // 空闲，可接受任务
    busy,       // 正在执行
    sleeping,   // 已落盘，可唤醒
};

/// Agent 定义（从 members/*.md 加载）
struct AgentDef {
    std::string agent_id;
    std::string name;
    std::string display_name;
    TeamRole role = TeamRole::member;
    std::string description;
    std::string model_override;
    std::vector<std::string> tools;      // 可见工具白名单，空=全部
    int max_steps = 0;                   // 0=使用默认
    std::filesystem::path workspace;     // 运行时确定的路径
};

/// 团队工作阶段
struct StageDef {
    std::string id;
    std::string description;
    std::vector<std::string> assigned_agents;  // 谁执行
    std::vector<std::string> depends_on;        // 依赖的前置 stage id
};

/// 团队定义（从 team.md 加载）
struct TeamDef {
    std::string team_id;
    std::string name;
    std::string description;
    TeamStrategy strategy = TeamStrategy::pipeline;
    int max_concurrent = 3;
    std::vector<AgentDef> members;
    std::vector<StageDef> stages;
    std::vector<std::string> shared_tools;  // 全员可用
    std::filesystem::path workspace;        // ~/.bengear/teams/{team_id}/
};

/// 团队执行状态（只读快照，不绑定 UI）
struct TeamStatus {
    std::string team_id;
    std::string execution_id;
    bool running = false;
    std::string current_stage;
    size_t completed_stages = 0;
    size_t total_stages = 0;
    struct MemberStatus {
        std::string agent_id;
        std::string name;
        AgentLifecycle state = AgentLifecycle::idle;
        bool has_error = false;
        std::string last_error;
    };
    std::vector<MemberStatus> members;
};

} // namespace ben_gear::team
