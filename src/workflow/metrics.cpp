#include "ben_gear/workflow/metrics.hpp"

namespace ben_gear {
namespace workflow {

std::string WorkflowMetrics::to_prometheus() const {
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

Json WorkflowMetrics::to_json() const {
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

void MetricsCollector::record_task_start(const std::string& task_id, const std::string& task_type) {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    task_start_times_[task_id] = now;
    task_types_[task_id] = task_type;
    current_parallel_tasks_++;
    parallel_tasks_peak_ = std::max(parallel_tasks_peak_, current_parallel_tasks_);
}

void MetricsCollector::record_task_complete(const std::string& task_id, const TaskResult& result) {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = task_start_times_.find(task_id);
    if (it != task_start_times_.end()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
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
    metrics_.total_tasks++;
    if (result.success) {
        metrics_.completed_tasks++;
    } else {
        metrics_.failed_tasks++;
    }
}

void MetricsCollector::record_tool_call() {
    std::unique_lock lock(mutex_);
    metrics_.total_tool_calls++;
}

void MetricsCollector::record_tokens(size_t tokens) {
    std::unique_lock lock(mutex_);
    metrics_.total_tokens += tokens;
}

void MetricsCollector::set_workflow_info(const std::string& workflow_id, const std::string& execution_id) {
    std::unique_lock lock(mutex_);
    metrics_.workflow_id = workflow_id;
    metrics_.execution_id = execution_id;
}

void MetricsCollector::set_total_duration(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    metrics_.total_duration = duration;
}

void MetricsCollector::calculate_cost(double cost_per_1k_tokens) {
    std::unique_lock lock(mutex_);
    metrics_.estimated_cost = (metrics_.total_tokens / 1000.0) * cost_per_1k_tokens;
}

WorkflowMetrics MetricsCollector::get_metrics() const {
    std::shared_lock lock(mutex_);
    return metrics_;
}

void MetricsCollector::reset() {
    std::unique_lock lock(mutex_);
    metrics_ = WorkflowMetrics{};
    task_start_times_.clear();
    task_types_.clear();
    current_parallel_tasks_ = 0;
    parallel_tasks_peak_ = 0;
}

} // namespace workflow
} // namespace ben_gear
