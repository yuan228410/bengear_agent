#include "ben_gear/workflow/storage.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

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

// ==================== FileStorage ====================

FileStorage::FileStorage(const std::string& base_dir)
    : base_dir_(base_dir) {
    // 创建基础目录
    std::filesystem::create_directories(base_dir_);
}

void FileStorage::save_workflow(const WorkflowId& id, const WorkflowState& state) {
    std::string path = get_workflow_path(id);
    std::ofstream file(path, std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    
    // 简化的序列化（实际项目中应使用 JSON 或 Protocol Buffers）
    // 这里仅作为示例
    file.write(reinterpret_cast<const char*>(&state), sizeof(state));
}

std::optional<WorkflowState> FileStorage::load_workflow(const WorkflowId& id) {
    std::string path = get_workflow_path(id);
    
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    
    std::ifstream file(path, std::ios::binary);
    
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    WorkflowState state;
    file.read(reinterpret_cast<char*>(&state), sizeof(state));
    
    return state;
}

void FileStorage::delete_workflow(const WorkflowId& id) {
    std::string path = get_workflow_path(id);
    
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

void FileStorage::save_task_result(
    const WorkflowId& workflow_id, 
    const TaskId& task_id, 
    const TaskResult& result) {
    
    std::string path = get_task_result_path(workflow_id, task_id);
    
    // 创建目录
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    
    std::ofstream file(path, std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    
    // 简化的序列化
    file.write(reinterpret_cast<const char*>(&result), sizeof(result));
}

std::optional<TaskResult> FileStorage::load_task_result(
    const WorkflowId& workflow_id,
    const TaskId& task_id) {
    
    std::string path = get_task_result_path(workflow_id, task_id);
    
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    
    std::ifstream file(path, std::ios::binary);
    
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    TaskResult result;
    file.read(reinterpret_cast<char*>(&result), sizeof(result));
    
    return result;
}

bool FileStorage::exists(const WorkflowId& id) {
    return std::filesystem::exists(get_workflow_path(id));
}

std::string FileStorage::get_workflow_path(const WorkflowId& id) const {
    return base_dir_ + "/" + id + ".workflow";
}

std::string FileStorage::get_task_result_path(
    const WorkflowId& workflow_id, 
    const TaskId& task_id) const {
    
    return base_dir_ + "/" + workflow_id + "/" + task_id + ".result";
}

} // namespace workflow
} // namespace ben_gear
