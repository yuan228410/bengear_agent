#pragma once

#include "task.hpp"
#include "base/utils/json.hpp"
#include "domain/event.hpp"
#include <chrono>
#include <map>
#include <string>
#include <shared_mutex>
#include <sstream>

namespace ben_gear {
namespace workflow {

/// 工作流性能指标
struct WorkflowMetrics {
    std::string workflow_id;
    std::string execution_id;

    // 时间指标
    std::chrono::milliseconds total_duration{0};
    std::chrono::milliseconds llm_time{0};
    std::chrono::milliseconds tool_time{0};
    std::chrono::milliseconds io_time{0};

    // 资源指标
    size_t total_tokens = 0;
    size_t total_tool_calls = 0;
    size_t parallel_tasks_peak = 0;

    // 成本指标
    double estimated_cost = 0.0;

    // 任务统计
    size_t total_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    size_t skipped_tasks = 0;

    /// 导出为 Prometheus 格式
    std::string to_prometheus() const;

    /// 导出为 JSON
    Json to_json() const;
};

/// 指标收集器
class MetricsCollector {
public:
    void record_task_start(const std::string& task_id, const std::string& task_type);
    void record_task_complete(const std::string& task_id, const TaskResult& result);
    void record_tool_call();
    void record_tokens(size_t tokens);
    void set_workflow_info(const std::string& workflow_id, const std::string& execution_id);
    void set_total_duration(std::chrono::milliseconds duration);
    void calculate_cost(double cost_per_1k_tokens = 0.002);
    WorkflowMetrics get_metrics() const;
    void reset();

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
