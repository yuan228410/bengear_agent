#include "workflow/dag.hpp"

#include <queue>
#include <stdexcept>

namespace ben_gear {
namespace workflow {

void DAG::add_task(const TaskId& id, TaskPtr task) {
    if (tasks_.find(id) != tasks_.end()) {
        throw std::invalid_argument("Task already exists: " + id);
    }
    tasks_[id] = std::move(task);
    dependencies_[id] = {};
    dependents_[id] = {};
}

void DAG::add_dependency(const TaskId& from, const TaskId& to) {
    if (tasks_.find(from) == tasks_.end()) {
        throw std::invalid_argument("Task not found: " + from);
    }
    if (tasks_.find(to) == tasks_.end()) {
        throw std::invalid_argument("Task not found: " + to);
    }
    if (can_reach(to, from)) {
        throw std::runtime_error("Adding dependency would create a cycle");
    }
    dependencies_[to].insert(from);
    dependents_[from].insert(to);
}

std::vector<TaskId> DAG::topological_sort() const {
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

std::vector<TaskId> DAG::get_ready_tasks(
        const std::unordered_set<TaskId>& completed_tasks) const {
    std::vector<TaskId> ready_tasks;
    for (const auto& [id, task] : tasks_) {
        if (completed_tasks.find(id) != completed_tasks.end()) {
            continue;
        }
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

bool DAG::has_cycle() const {
    std::unordered_set<TaskId> visited;
    std::unordered_set<TaskId> rec_stack;
    for (const auto& [id, task] : tasks_) {
        if (has_cycle_dfs(id, visited, rec_stack)) {
            return true;
        }
    }
    return false;
}

TaskPtr DAG::get_task(const TaskId& id) const {
    auto it = tasks_.find(id);
    return it != tasks_.end() ? it->second : nullptr;
}

std::vector<TaskId> DAG::get_all_task_ids() const {
    std::vector<TaskId> ids;
    ids.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        ids.push_back(id);
    }
    return ids;
}

bool DAG::can_reach(const TaskId& start, const TaskId& target) const {
    std::unordered_set<TaskId> visited;
    std::queue<TaskId> queue;
    queue.push(start);
    visited.insert(start);
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        auto dep_it = dependents_.find(current);
        if (dep_it == dependents_.end()) continue;
        for (const auto& next : dep_it->second) {
            if (next == target) return true;
            if (visited.find(next) == visited.end()) {
                visited.insert(next);
                queue.push(next);
            }
        }
    }
    return false;
}

bool DAG::has_cycle_dfs(const TaskId& id,
                        std::unordered_set<TaskId>& visited,
                        std::unordered_set<TaskId>& rec_stack) const {
    if (rec_stack.find(id) != rec_stack.end()) return true;
    if (visited.find(id) != visited.end()) return false;

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

} // namespace workflow
} // namespace ben_gear
