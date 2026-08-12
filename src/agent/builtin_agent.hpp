#pragma once

#include <string>
#include <vector>

namespace ben_gear::agent {

// ─── Agent 分类 ─────────────────────────────────────────────────

/// Agent 三大类别
enum class AgentCategory : uint8_t {
    primary,   ///< 主 Agent — 运行在 ExecutionLoop，拥有完整工具权限
    sub,       ///< 子 Agent — 通过 SubAgentRuntime 委派执行，工具受限
    team,      ///< 团队 Agent — 通过 TeamOrchestrator 多 Agent 协作
};

/// primary agent 的执行策略
enum class ExecutionMode : uint8_t {
    react,     ///< ReAct 循环，直接执行
    plan,      ///< 计划模式：LLM 先规划、审批后逐步执行
};

// ─── Agent 定义 ─────────────────────────────────────────────────

/// 内置 Agent 定义 — 代码级注册的 agent（不同于 .md 文件自定义的 sub_agent）
///
/// 三种分类：
///   primary  — build / plan，主 Agent 的两个变体
///   sub      — 自定义子 Agent（通过 sub_agents/*.md 定义）
///   team     — 团队 Agent（通过 teams/*/team.md 定义）
///
/// primary agent 由 SessionRunner 根据 ExecutionMode 选择 Interceptor 组合。
struct BuiltinAgentDef {
    std::string name;                  ///< @ 触发名
    std::string description;           ///< 前端补全描述
    AgentCategory category = AgentCategory::primary;
    ExecutionMode mode = ExecutionMode::react;  ///< 仅 primary agent 使用
    std::string system_prompt;         ///< 注入的 system prompt（空=默认）
};

// ─── 注册表 ─────────────────────────────────────────────────────

class BuiltinAgentRegistry {
public:
    void register_agent(BuiltinAgentDef def);

    const BuiltinAgentDef* find(std::string_view name) const;

    /// 按分类过滤
    std::vector<BuiltinAgentDef> by_category(AgentCategory cat) const;

    const std::vector<BuiltinAgentDef>& agents() const { return agents_; }

    /// 从目录加载 .md 文件注册内置 agent
    /// 文件格式同 sub_agent，frontmatter 额外支持 category/mode 字段
    static BuiltinAgentRegistry load_from_directory(const std::string& directory);

private:
    std::vector<BuiltinAgentDef> agents_;
};

} // namespace ben_gear::agent
