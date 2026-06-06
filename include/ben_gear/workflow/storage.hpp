#pragma once

#include "task.hpp"
#include "types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <map>

namespace ben_gear {
namespace workflow {

// 工作流存储接口
class IWorkflowStorage {
public:
    virtual ~IWorkflowStorage() = default;
    
    // 保存工作流状态
    virtual void save_workflow(const WorkflowId& id, const WorkflowState& state) = 0;
    
    // 加载工作流状态
    virtual std::optional<WorkflowState> load_workflow(const WorkflowId& id) = 0;
    
    // 删除工作流状态
    virtual void delete_workflow(const WorkflowId& id) = 0;
    
    // 保存任务结果
    virtual void save_task_result(
        const WorkflowId& workflow_id, 
        const TaskId& task_id, 
        const TaskResult& result) = 0;
    
    // 加载任务结果
    virtual std::optional<TaskResult> load_task_result(
        const WorkflowId& workflow_id,
        const TaskId& task_id) = 0;
    
    // 检查工作流是否存在
    virtual bool exists(const WorkflowId& id) = 0;
};

// 内存存储（默认实现）
class MemoryStorage : public IWorkflowStorage {
public:
    void save_workflow(const WorkflowId& id, const WorkflowState& state) override;
    std::optional<WorkflowState> load_workflow(const WorkflowId& id) override;
    void delete_workflow(const WorkflowId& id) override;
    
    void save_task_result(
        const WorkflowId& workflow_id, 
        const TaskId& task_id, 
        const TaskResult& result) override;
    
    std::optional<TaskResult> load_task_result(
        const WorkflowId& workflow_id,
        const TaskId& task_id) override;
    
    bool exists(const WorkflowId& id) override;
    
    // 清空所有数据
    void clear();
    
    // 获取工作流数量
    size_t size() const;
    
private:
    std::map<WorkflowId, WorkflowState> workflows_;
    std::map<std::pair<WorkflowId, TaskId>, TaskResult> task_results_;
};

// 文件存储
class FileStorage : public IWorkflowStorage {
public:
    explicit FileStorage(const std::string& base_dir = "./workflow_data");
    
    void save_workflow(const WorkflowId& id, const WorkflowState& state) override;
    std::optional<WorkflowState> load_workflow(const WorkflowId& id) override;
    void delete_workflow(const WorkflowId& id) override;
    
    void save_task_result(
        const WorkflowId& workflow_id, 
        const TaskId& task_id, 
        const TaskResult& result) override;
    
    std::optional<TaskResult> load_task_result(
        const WorkflowId& workflow_id,
        const TaskId& task_id) override;
    
    bool exists(const WorkflowId& id) override;
    
private:
    std::string base_dir_;
    
    // 获取工作流文件路径
    std::string get_workflow_path(const WorkflowId& id) const;
    
    // 获取任务结果文件路径
    std::string get_task_result_path(const WorkflowId& workflow_id, 
                                      const TaskId& task_id) const;
};

} // namespace workflow
} // namespace ben_gear
