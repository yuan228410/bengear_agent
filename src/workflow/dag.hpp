#pragma once

#include "types.hpp"
#include "task.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ben_gear {
namespace workflow {

/// DAG（有向无环图）管理器
class DAG {
public:
    /// 添加任务节点
    void add_task(const TaskId& id, TaskPtr task);

    /// 添加依赖关系（to 依赖 from）
    void add_dependency(const TaskId& from, const TaskId& to);

    /// 拓扑排序（Kahn 算法）
    std::vector<TaskId> topological_sort() const;

    /// 获取可执行任务（依赖已满足）
    std::vector<TaskId> get_ready_tasks(
        const std::unordered_set<TaskId>& completed_tasks) const;

    /// 检测环（DFS）
    bool has_cycle() const;

    /// 获取任务
    TaskPtr get_task(const TaskId& id) const;

    /// 获取所有任务 ID
    std::vector<TaskId> get_all_task_ids() const;

    /// 获取任务数量
    size_t size() const { return tasks_.size(); }

    /// 是否为空
    bool empty() const { return tasks_.empty(); }

private:
    /// BFS: 从 start 能否沿 dependents 前向边到达 target
    bool can_reach(const TaskId& start, const TaskId& target) const;

    /// DFS 检测环
    bool has_cycle_dfs(const TaskId& id,
                       std::unordered_set<TaskId>& visited,
                       std::unordered_set<TaskId>& rec_stack) const;

    std::unordered_map<TaskId, TaskPtr> tasks_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> dependencies_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> dependents_;
};

} // namespace workflow
} // namespace ben_gear
