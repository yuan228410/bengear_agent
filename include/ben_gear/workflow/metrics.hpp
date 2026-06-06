#pragma once

#include "types.hpp"
#include <chrono>
#include <map>
#include <string>

namespace ben_gear {
namespace workflow {

/// 工作流性能指标
struct WorkflowMetrics {
    std::string workflow_id;
    std::string execution_id;
    
    // 时间指标
    std::chrono::milliseconds total_duration{0};
    std::chrono::milliseconds llm_time{0};        // LLM 调用总时长
    std::chrono::milliseconds tool_time{0};       // 工具执行总时长
    std::chrono::milliseconds io_time{0};         // I/O 等待时长
    
    // 资源指标
    size_t total_tokens = 0;                      // Token 消耗
    size_t total_tool_calls = 0;                  // 工具调用次数
    size_t parallel_tasks_peak = 0;               // 并行任务峰值
    
    // 成本指标
    double estimated_cost = 0.0;                  // 预估成本（美元）
    
    // 任务统计
    size_t total_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    size_t skipped_tasks = 0;
    
    /// 导出为 Prometheus 格式
    std::string to_prometheus() const {
        std::ostringstream oss;
        oss << "# HELP workflow_duration_ms Total workflow execution duration\n";
        oss << "# TYPE workflow_duration_ms gauge\n";
        oss << "workflow_duration_ms{workflow_id=\"" << workflow_id << "\"} " 
            << total_duration.count() << "\n";
        
        oss << "# HELP workflow_tasks_total Total number of tasks\n";
        oss << "# TYPE workflow_tasks_total gauge\n";
        oss << "workflow_tasks_total{workflow_id=\"" << workflow_id << "\"} " 
            << total_tasks << "\n";
        
        oss << "# HELP workflow_tasks_completed Completed tasks\n";
        oss << "# TYPE workflow_tasks_completed gauge\n";
        oss << "workflow_tasks_completed{workflow_id=\"" << workflow_id << "\"} " 
            << completed_tasks << "\n";
        
        oss << "# HELP workflow_tasks_failed Failed tasks\n";
        oss << "# TYPE workflow_tasks_failed gauge\n";
        oss << "workflow_tasks_failed{workflow_id=\"" << workflow_id << "\"} " 
            << failed_tasks << "\n";
        
        oss << "# HELP workflow_tokens_total Total tokens consumed\n";
        oss << "# TYPE workflow_tokens_total counter\n";
        oss << "workflow_tokens_total{workflow_id=\"" << workflow_id << "\"} " 
            << total_tokens << "\n";
        
        oss << "# HELP workflow_cost_dollars Estimated cost in dollars\n";
        oss << "# TYPE workflow_cost_dollars gauge\n";
        oss << "workflow_cost_dollars{workflow_id=\"" << workflow_id << "\"} " 
            << estimated_cost << "\n";
        
        return oss.str();
    }
    
    /// 导出为 JSON
    Json to_json() const {
        Json j;
        j["workflow_id"] = workflow_id;
        j["execution_id"] = execution_id;
        j["total_duration_ms"] = total_duration.count();
        j["llm_time_ms"] = llm_time.count();
        j["tool_time_ms"] = tool_time.count();
        j["io_time_ms"] = io_time.count();
        j["total_tokens"] = total_tokens;
        j["total_tool_calls"] = total_tool_calls;
        j["parallel_tasks_peak"] = parallel_tasks_peak;
        j["estimated_cost"] = estimated_cost;
        j["total_tasks"] = total_tasks;
        j["completed_tasks"] = completed_tasks;
        j["failed_tasks"] = failed_tasks;
        j["skipped_tasks"] = skipped_tasks;
        return j;
    }
};

/// 工作流进度回调
class WorkflowProgressCallbacks {
public:
    virtual ~WorkflowProgressCallbacks() = default;
    
    /// 任务开始
    virtual void on_task_started(const std::string& task_id) {
        (void)task_id;  // 避免未使用参数告警
    }
    
    /// 任务进度更新（0-100）
    virtual void on_task_progress(const std::string& task_id, int progress) {
        (void)task_id;
        (void)progress;
    }
    
    /// 任务完成
    virtual void on_task_completed(const std::string& task_id, const TaskResult& result) {
        (void)task_id;
        (void)result;
    }
    
    /// 工作流整体进度
    virtual void on_workflow_progress(int completed, int total) {
        (void)completed;
        (void)total;
    }
    
    /// 工作流开始
    virtual void on_workflow_started(const std::string& workflow_id) {
        (void)workflow_id;
    }
    
    /// 工作流完成
    virtual void on_workflow_completed(const std::string& workflow_id, const WorkflowState& state) {
        (void)workflow_id;
        (void)state;
    }
};

/// 空回调实现
class NullWorkflowProgressCallbacks : public WorkflowProgressCallbacks {};

/// 指标收集器
class MetricsCollector {
public:
    /// 记录任务开始
    void record_task_start(const std::string& task_id, const std::string& task_type) {
        std::unique_lock lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        task_start_times_[task_id] = now;
        task_types_[task_id] = task_type;
        
        // 更新并行任务数
        current_parallel_tasks_++;
        parallel_tasks_peak_ = std::max(parallel_tasks_peak_, current_parallel_tasks_);
    }
    
    /// 记录任务完成
    void record_task_complete(const std::string& task_id, const TaskResult& result) {
        std::unique_lock lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        auto it = task_start_times_.find(task_id);
        if (it != task_start_times_.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
            
            // 按类型统计时间
            auto type_it = task_types_.find(task_id);
            if (type_it != task_types_.end()) {
                if (type_it->second == "llm") {
                    metrics_.llm_time += duration;
                } else if (type_it->second == "tool") {
                    metrics_.tool_time += duration;
                }
            }
            
            task_start_times_.erase(it);
        }
        
        current_parallel_tasks_--;
        
        // 更新统计
        metrics_.total_tasks++;
        if (result.success) {
            metrics_.completed_tasks++;
        } else {
            metrics_.failed_tasks++;
        }
    }
    
    /// 记录工具调用
    void record_tool_call() {
        std::unique_lock lock(mutex_);
        metrics_.total_tool_calls++;
    }
    
    /// 记录 Token 消耗
    void record_tokens(size_t tokens) {
        std::unique_lock lock(mutex_);
        metrics_.total_tokens += tokens;
    }
    
    /// 设置工作流信息
    void set_workflow_info(const std::string& workflow_id, const std::string& execution_id) {
        std::unique_lock lock(mutex_);
        metrics_.workflow_id = workflow_id;
        metrics_.execution_id = execution_id;
    }
    
    /// 设置总时长
    void set_total_duration(std::chrono::milliseconds duration) {
        std::unique_lock lock(mutex_);
        metrics_.total_duration = duration;
    }
    
    /// 计算预估成本
    void calculate_cost(double cost_per_1k_tokens = 0.002) {
        std::unique_lock lock(mutex_);
        metrics_.estimated_cost = (metrics_.total_tokens / 1000.0) * cost_per_1k_tokens;
    }
    
    /// 获取指标
    WorkflowMetrics get_metrics() const {
        std::shared_lock lock(mutex_);
        return metrics_;
    }
    
    /// 重置
    void reset() {
        std::unique_lock lock(mutex_);
        metrics_ = WorkflowMetrics{};
        task_start_times_.clear();
        task_types_.clear();
        current_parallel_tasks_ = 0;
        parallel_tasks_peak_ = 0;
    }
    
private:
    WorkflowMetrics metrics_;
    std::map<std::string, std::chrono::steady_clock::time_point> task_start_times_;
    std::map<std::string, std::string> task_types_;
    size_t current_parallel_tasks_ = 0;
    size_t parallel_tasks_peak_ = 0;
    mutable std::shared_mutex mutex_;
};

} // namespace workflow
} // namespace ben_gear
