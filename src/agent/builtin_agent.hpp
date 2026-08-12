#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ben_gear::capabilities::tool { class ToolRegistry; }

namespace ben_gear::agent {

// ─── Agent 分类 ─────────────────────────────────────────────────

enum class AgentCategory : uint8_t {
    primary,   ///< 主 Agent — 运行在 ExecutionLoop，拥有完整工具权限
    sub,       ///< 子 Agent — 通过 SubAgentRuntime 委派执行，工具受限
    team,      ///< 团队 Agent — 通过 TeamOrchestrator 多 Agent 协作
};

enum class ExecutionMode : uint8_t {
    react,     ///< ReAct 循环，直接执行
    plan,      ///< 计划模式：LLM 先规划、审批后逐步执行
};

// ─── Agent 定义 ─────────────────────────────────────────────────

struct BuiltinAgentDef {
    std::string name;
    std::string description;
    AgentCategory category = AgentCategory::primary;
    ExecutionMode mode = ExecutionMode::react;
    std::string system_prompt;
    std::vector<std::string> tools;    ///< 工具白名单（空=全部可见）
};

// ─── 注册表 ─────────────────────────────────────────────────────

class BuiltinAgentRegistry {
public:
    void register_agent(BuiltinAgentDef def);
    const BuiltinAgentDef* find(std::string_view name) const;
    std::vector<BuiltinAgentDef> by_category(AgentCategory cat) const;
    const std::vector<BuiltinAgentDef>& agents() const { return agents_; }
    static BuiltinAgentRegistry load_from_directory(const std::string& directory);

private:
    std::vector<BuiltinAgentDef> agents_;
};

// ─── 工具注册 ───────────────────────────────────────────────────

void register_primary_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<BuiltinAgentRegistry> agent_reg,
    const std::string& workspace_dir,
    const std::string& data_dir);

} // namespace ben_gear::agent
