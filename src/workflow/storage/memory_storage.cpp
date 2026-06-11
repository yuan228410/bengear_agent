#include "ben_gear/workflow/storage.hpp"

namespace ben_gear {
namespace workflow {

// ==================== MemoryStorage ====================

void MemoryStorage::save_workflow(const WorkflowId& id, const WorkflowState& state) {
    workflows_[id] = state;
}

std::optional<WorkflowState> MemoryStorage::load_workflow(const WorkflowId& id) {
    auto it = workflows_.find(id);
    if (it != workflows_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void MemoryStorage::delete_workflow(const WorkflowId& id) {
    workflows_.erase(id);
    // 删除相关的任务结果
    auto it = task_results_.begin();
    while (it != task_results_.end()) {
        if (it->first.first == id) {
            it = task_results_.erase(it);
        } else {
            ++it;
        }
    }
}

void MemoryStorage::save_task_result(
    const WorkflowId& workflow_id, 
    const TaskId& task_id, 
    const TaskResult& result) {
    
    task_results_[{workflow_id, task_id}] = result;
}

std::optional<TaskResult> MemoryStorage::load_task_result(
    const WorkflowId& workflow_id,
    const TaskId& task_id) {
    
    auto it = task_results_.find({workflow_id, task_id});
    if (it != task_results_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MemoryStorage::exists(const WorkflowId& id) {
    return workflows_.find(id) != workflows_.end();
}

void MemoryStorage::clear() {
    workflows_.clear();
    task_results_.clear();
}

size_t MemoryStorage::size() const {
    return workflows_.size();
}

} // namespace workflow
} // namespace ben_gear
