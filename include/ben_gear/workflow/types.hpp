#pragma once

#include <string>
#include <cstdint>
#include <map>
#include <chrono>

namespace ben_gear {
namespace workflow {

// 前向声明
struct TaskResult;

// 任务 ID 类型
using TaskId = std::string;

// 工作流 ID 类型
using WorkflowId = std::string;

// 任务状态
enum class TaskStatus {
    PENDING,      // 待执行
    RUNNING,      // 执行中
    SUCCESS,      // 成功
    FAILED,       // 失败
    CANCELLED     // 已取消
};

// 工作流状态
enum class WorkflowStatus {
    PENDING,      // 待执行
    RUNNING,      // 执行中
    PAUSED,       // 已暂停
    SUCCESS,      // 成功
    FAILED,       // 失败
    CANCELLED     // 已取消
};

// 错误处理策略
enum class ErrorHandlingStrategy {
    FAIL_FAST,    // 遇到错误立即停止
    CONTINUE,     // 继续执行其他任务
    RETRY         // 重试失败任务
};

// 重试策略
struct RetryPolicy {
    size_t max_retries = 3;
    uint32_t retry_delay_ms = 1000;
    bool exponential_backoff = true;
};

// 工作流状态
struct WorkflowState {
    std::string id;                                      // 执行 ID
    WorkflowStatus status = WorkflowStatus::PENDING;     // 整体状态
    std::string error_message;                           // 错误信息
    std::map<TaskId, TaskResult> task_results;           // 任务结果
    
    std::chrono::system_clock::time_point started_at;    // 开始时间
    std::chrono::system_clock::time_point completed_at;  // 完成时间
};

// 工作流执行结果
struct WorkflowResult {
    bool success = false;
    std::string error_message;
    std::map<TaskId, TaskResult> task_results;  // 各任务执行结果
    WorkflowStatus status = WorkflowStatus::PENDING;
    
    // 统计信息
    size_t total_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    uint64_t execution_time_ms = 0;
};

// 工作流状态快照
struct WorkflowStatusSnapshot {
    bool running = false;
    size_t total_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    std::string current_task;
};

} // namespace workflow
} // namespace ben_gear
