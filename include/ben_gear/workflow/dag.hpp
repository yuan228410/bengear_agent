#pragma once

#include "types.hpp"
#include "task.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <stdexcept>
#include <algorithm>

namespace ben_gear {
namespace workflow {

// DAG（有向无环图）管理器
class DAG {
public:
    // 添加任务节点
    void add_task(const TaskId& id, TaskPtr task) {
        if (tasks_.find(id) != tasks_.end()) {
            throw std::invalid_argument("Task already exists: " + id);
        }
        tasks_[id] = std::move(task);
        dependencies_[id] = {};  // 初始化依赖列表
        dependents_[id] = {};    // 初始化被依赖列表
    }
    
    // 添加依赖关系（to 依赖 from）
    void add_dependency(const TaskId& from, const TaskId& to) {
        if (tasks_.find(from) == tasks_.end()) {
            throw std::invalid_argument("Task not found: " + from);
        }
        if (tasks_.find(to) == tasks_.end()) {
            throw std::invalid_argument("Task not found: " + to);
        }
        
        dependencies_[to].insert(from);
        dependents_[from].insert(to);
        
        // 检测环
        if (has_cycle()) {
            // 回滚
            dependencies_[to].erase(from);
            dependents_[from].erase(to);
            throw std::runtime_error("Adding dependency would create a cycle");
        }
    }
    
    // 拓扑排序（Kahn 算法）
    std::vector<TaskId> topological_sort() const {
        std::unordered_map<TaskId, size_t> in_degree;
        for (const auto& [id, task] : tasks_) {
            in_degree[id] = dependencies_.at(id).size();
        }
        
        std::queue<TaskId> queue;
        for (const auto& [id, degree] : in_degree) {
            if (degree == 0) {
                queue.push(id);
            }
        }
        
        std::vector<TaskId> result;
        while (!queue.empty()) {
            auto current = queue.front();
            queue.pop();
            result.push_back(current);
            
            for (const auto& dependent : dependents_.at(current)) {
                in_degree[dependent]--;
                if (in_degree[dependent] == 0) {
                    queue.push(dependent);
                }
            }
        }
        
        return result;
    }
    
    // 获取可执行任务（依赖已满足）
    std::vector<TaskId> get_ready_tasks(
        const std::unordered_set<TaskId>& completed_tasks) const {
        
        std::vector<TaskId> ready_tasks;
        
        for (const auto& [id, task] : tasks_) {
            // 跳过已完成的任务
            if (completed_tasks.find(id) != completed_tasks.end()) {
                continue;
            }
            
            // 检查所有依赖是否已完成
            bool all_deps_completed = true;
            for (const auto& dep : dependencies_.at(id)) {
                if (completed_tasks.find(dep) == completed_tasks.end()) {
                    all_deps_completed = false;
                    break;
                }
            }
            
            if (all_deps_completed) {
                ready_tasks.push_back(id);
            }
        }
        
        return ready_tasks;
    }
    
    // 检测环（DFS）
    bool has_cycle() const {
        std::unordered_set<TaskId> visited;
        std::unordered_set<TaskId> rec_stack;
        
        for (const auto& [id, task] : tasks_) {
            if (has_cycle_dfs(id, visited, rec_stack)) {
                return true;
            }
        }
        
        return false;
    }
    
    // 获取任务
    TaskPtr get_task(const TaskId& id) const {
        auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            return nullptr;
        }
        return it->second;
    }
    
    // 获取所有任务 ID
    std::vector<TaskId> get_all_task_ids() const {
        std::vector<TaskId> ids;
        for (const auto& [id, task] : tasks_) {
            ids.push_back(id);
        }
        return ids;
    }
    
    // 获取任务数量
    size_t size() const { return tasks_.size(); }
    
    // 是否为空
    bool empty() const { return tasks_.empty(); }
    
private:
    // DFS 检测环
    bool has_cycle_dfs(const TaskId& id,
                       std::unordered_set<TaskId>& visited,
                       std::unordered_set<TaskId>& rec_stack) const {
        if (rec_stack.find(id) != rec_stack.end()) {
            return true;  // 发现环
        }
        
        if (visited.find(id) != visited.end()) {
            return false;  // 已访问过
        }
        
        visited.insert(id);
        rec_stack.insert(id);
        
        for (const auto& dependent : dependents_.at(id)) {
            if (has_cycle_dfs(dependent, visited, rec_stack)) {
                return true;
            }
        }
        
        rec_stack.erase(id);
        return false;
    }
    
private:
    std::unordered_map<TaskId, TaskPtr> tasks_;
    std::unordered_map<TaskId, std::unordered_set<TaskId>> dependencies_;  // 任务依赖的前置任务
    std::unordered_map<TaskId, std::unordered_set<TaskId>> dependents_;    // 依赖此任务的后继任务
};

} // namespace workflow
} // namespace ben_gear
