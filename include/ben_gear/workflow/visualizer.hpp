#pragma once

#include "types.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <string>
#include <map>

namespace ben_gear {
namespace workflow {

struct WorkflowDefinition;

/// 工作流可视化器
class WorkflowVisualizer {
public:
    /// 生成 DOT 格式（Graphviz）
    std::string to_dot(const WorkflowDefinition& workflow) const;

    /// 生成 Mermaid 格式
    std::string to_mermaid(const WorkflowDefinition& workflow) const;

    /// 生成执行状态图（带颜色标记）
    std::string to_mermaid_with_state(
        const WorkflowDefinition& workflow,
        const WorkflowState& state) const;

    /// 生成 ASCII 艺术图（简单版）
    std::string to_ascii(const WorkflowDefinition& workflow) const;

private:
    std::string get_task_color(const std::string& type) const;
    std::string sanitize_id(const std::string& id) const;
    std::vector<std::string> topological_sort(const WorkflowDefinition& workflow) const;
};

} // namespace workflow
} // namespace ben_gear
