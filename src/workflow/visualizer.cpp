#include "ben_gear/workflow/visualizer.hpp"
#include "ben_gear/workflow/workflow_engine.hpp"
#include <sstream>
#include <algorithm>

namespace ben_gear {
namespace workflow {

std::string WorkflowVisualizer::get_task_color(const std::string& type) const {
    if (type == "llm") return "lightgreen";
    if (type == "tool") return "lightyellow";
    if (type == "condition") return "lightpink";
    if (type == "subflow") return "lightcyan";
    return "lightblue";
}

std::string WorkflowVisualizer::sanitize_id(const std::string& id) const {
    std::string result;
    for (char c : id) {
        if (std::isalnum(c) || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }
    return result;
}

std::vector<std::string> WorkflowVisualizer::topological_sort(const WorkflowDefinition& workflow) const {
    std::map<std::string, int> in_degree;
    std::map<std::string, std::vector<std::string>> graph;

    for (const auto& task : workflow.tasks) {
        in_degree[task.id] = 0;
        graph[task.id] = {};
    }
    for (const auto& task : workflow.tasks) {
        for (const auto& dep : task.depends_on) {
            graph[dep].push_back(task.id);
            in_degree[task.id]++;
        }
    }

    std::vector<std::string> result;
    std::vector<std::string> queue;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) queue.push_back(id);
    }

    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();
        result.push_back(current);
        for (const auto& neighbor : graph[current]) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) queue.push_back(neighbor);
        }
    }
    return result;
}

std::string WorkflowVisualizer::to_dot(const WorkflowDefinition& workflow) const {
    std::ostringstream oss;
    oss << "digraph Workflow {\n";
    oss << "    rankdir=TB;\n";
    oss << "    node [shape=box, style=\"rounded,filled\", fillcolor=lightblue];\n";
    oss << "    edge [color=gray];\n\n";

    for (const auto& task : workflow.tasks) {
        std::string label = task.name.empty() ? task.id : task.name;
        std::string color = get_task_color(task.type);
        oss << "    \"" << task.id << "\" [label=\"" << label
            << "\", fillcolor=\"" << color << "\"];\n";
    }
    oss << "\n";
    for (const auto& task : workflow.tasks) {
        for (const auto& dep : task.depends_on) {
            oss << "    \"" << dep << "\" -> \"" << task.id << "\";\n";
        }
    }
    oss << "}\n";
    return oss.str();
}

std::string WorkflowVisualizer::to_mermaid(const WorkflowDefinition& workflow) const {
    std::ostringstream oss;
    oss << "```mermaid\n";
    oss << "graph TD\n";
    for (const auto& task : workflow.tasks) {
        std::string label = task.name.empty() ? task.id : task.name;
        oss << "    " << sanitize_id(task.id) << "[" << label << "]\n";
    }
    oss << "\n";
    for (const auto& task : workflow.tasks) {
        for (const auto& dep : task.depends_on) {
            oss << "    " << sanitize_id(dep) << " --> " << sanitize_id(task.id) << "\n";
        }
    }
    oss << "```\n";
    return oss.str();
}

std::string WorkflowVisualizer::to_mermaid_with_state(
    const WorkflowDefinition& workflow,
    const WorkflowState& state) const {

    std::ostringstream oss;
    oss << "```mermaid\n";
    oss << "graph TD\n";
    oss << "    classDef completed fill:#90EE90,stroke:#2E7D32\n";
    oss << "    classDef running fill:#FFD700,stroke:#F57C00\n";
    oss << "    classDef failed fill:#FF6B6B,stroke:#C62828\n";
    oss << "    classDef pending fill:#D3D3D3,stroke:#757575\n\n";

    for (const auto& task : workflow.tasks) {
        std::string label = task.name.empty() ? task.id : task.name;
        std::string node_id = sanitize_id(task.id);
        oss << "    " << node_id << "[" << label << "]";

        auto it = state.task_results.find(task.id);
        if (it != state.task_results.end()) {
            oss << ":::" << (it->second.success ? "completed" : "failed");
        } else {
            oss << ":::pending";
        }
        oss << "\n";
    }
    oss << "\n";
    for (const auto& task : workflow.tasks) {
        for (const auto& dep : task.depends_on) {
            oss << "    " << sanitize_id(dep) << " --> " << sanitize_id(task.id) << "\n";
        }
    }
    oss << "```\n";
    return oss.str();
}

std::string WorkflowVisualizer::to_ascii(const WorkflowDefinition& workflow) const {
    std::ostringstream oss;
    oss << "Workflow: " << workflow.name << "\n";
    oss << std::string(workflow.name.size() + 10, '=') << "\n\n";

    auto sorted = topological_sort(workflow);
    std::vector<std::vector<std::string>> levels;
    std::map<std::string, int> task_levels;

    for (const auto& task_id : sorted) {
        int level = 0;
        for (const auto& task : workflow.tasks) {
            if (task.id == task_id) {
                for (const auto& dep : task.depends_on) {
                    auto it = task_levels.find(dep);
                    if (it != task_levels.end()) {
                        level = std::max(level, it->second + 1);
                    }
                }
            }
        }
        task_levels[task_id] = level;
        if (level >= static_cast<int>(levels.size())) levels.resize(level + 1);
        levels[level].push_back(task_id);
    }

    for (size_t i = 0; i < levels.size(); ++i) {
        oss << "Level " << i << ":\n";
        for (const auto& task_id : levels[i]) {
            oss << "  [" << task_id << "]\n";
        }
        if (i < levels.size() - 1) {
            oss << "    |\n    v\n";
        }
    }
    return oss.str();
}

} // namespace workflow
} // namespace ben_gear
